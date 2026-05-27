#!/usr/bin/env bash
# OTF Browser — Linux auto-download & install script
#
# Usage:
#   curl -fsSL https://browser.opentechf.org/install.sh | sh
#
# Supported targets:
#   Debian / Ubuntu          → .deb (apt)
#   Fedora / RHEL / Rocky    → .rpm (dnf)
#   Arch / Manjaro           → .tar.gz → /opt/otf-browser + symlink
#   Other (AppImage)         → .AppImage → /opt/otf-browser
#
set -euo pipefail

REPO="Open-Tech-Foundation/Web-Browser"
INSTALL_DIR="/opt/otf-browser"

# ── helpers ──────────────────────────────────────────────────────────────────
info()  { printf "\033[1m%s\033[0m\n" "$*"; }
ok()    { printf "\033[32m%s\033[0m\n" "$*"; }
err()   { printf "\033[31m%s\033[0m\n" "$*" >&2; }

cleanup() {
  [ -n "${TMPDIR:-}" ] && rm -rf "$TMPDIR"
}
trap cleanup EXIT

# ── detect latest release ────────────────────────────────────────────────────
info "Fetching latest release info…"
API="https://api.github.com/repos/$REPO/releases?per_page=1"
JSON="$(curl -fsSL "$API")"

TAG="$(echo "$JSON" | grep -m1 '"tag_name"' | cut -d'"' -f4)"
VERSION="$(echo "$TAG" | sed 's/^v//')"
[ -z "$VERSION" ] && { err "Could not determine latest version"; exit 1; }

ok "Latest release: $TAG"

# ── detect platform ─────────────────────────────────────────────────────────
ARCH="$(uname -m)"
case "$ARCH" in
  x86_64|amd64) ARCH_SHORT="x64" ;;
  aarch64|arm64) ARCH_SHORT="arm64" ;;
  *) err "Unsupported architecture: $ARCH"; exit 1 ;;
esac

DOWNLOAD_URL=""
PKG_TYPE=""

install_deb() {
  PKG_TYPE="deb"
  DOWNLOAD_URL="https://github.com/$REPO/releases/download/$TAG/otf-browser_${VERSION}_amd64.deb"
  info "Downloading .deb package…"
  curl -fsSL -o "$TMPDIR/otf-browser.deb" "$DOWNLOAD_URL"
  info "Installing with apt…"
  sudo apt-get update -qq
  sudo apt-get install -y -f "$TMPDIR/otf-browser.deb"
  ok "Installation complete. Run 'otf-browser' from terminal or app menu."
}

install_rpm() {
  PKG_TYPE="rpm"
  DOWNLOAD_URL="https://github.com/$REPO/releases/download/$TAG/otf-browser-${VERSION}-1.x86_64.rpm"
  info "Downloading .rpm package…"
  curl -fsSL -o "$TMPDIR/otf-browser.rpm" "$DOWNLOAD_URL"
  info "Installing with dnf…"
  sudo dnf install -y "$TMPDIR/otf-browser.rpm"
  ok "Installation complete. Run 'otf-browser' from terminal or app menu."
}

install_archive() {
  PKG_TYPE="tar.gz"
  DOWNLOAD_URL="https://github.com/$REPO/releases/download/$TAG/otf-browser-linux-${ARCH_SHORT}-${TAG}.tar.gz"
  info "Downloading archive…"
  curl -fsSL -o "$TMPDIR/otf-browser.tar.gz" "$DOWNLOAD_URL"
  info "Extracting to $INSTALL_DIR…"
  sudo mkdir -p "$INSTALL_DIR"
  sudo tar -xzf "$TMPDIR/otf-browser.tar.gz" -C "$INSTALL_DIR" --strip-components=1
  sudo ln -sf "$INSTALL_DIR/otf-browser" /usr/bin/otf-browser
  # chrome-sandbox SUID
  SANDBOX="$INSTALL_DIR/chrome-sandbox"
  [ -f "$SANDBOX" ] && sudo chown root:root "$SANDBOX" && sudo chmod 4755 "$SANDBOX"
  # desktop entry
  sudo mkdir -p /usr/share/applications
  sudo cp "$INSTALL_DIR/otf-browser.desktop" /usr/share/applications/ 2>/dev/null || true
  ok "Installation complete. Run 'otf-browser' from terminal or app menu."
}

install_appimage() {
  PKG_TYPE="AppImage"
  DOWNLOAD_URL="https://github.com/$REPO/releases/download/$TAG/otf-browser-linux-${ARCH_SHORT}-${TAG}.AppImage"
  info "Downloading AppImage…"
  sudo mkdir -p "$INSTALL_DIR"
  sudo curl -fsSL -o "$INSTALL_DIR/otf-browser" "$DOWNLOAD_URL"
  sudo chmod +x "$INSTALL_DIR/otf-browser"
  sudo ln -sf "$INSTALL_DIR/otf-browser" /usr/bin/otf-browser
  # chrome-sandbox SUID
  SANDBOX="$INSTALL_DIR/chrome-sandbox"
  [ -f "$SANDBOX" ] && sudo chown root:root "$SANDBOX" && sudo chmod 4755 "$SANDBOX"
  ok "Installation complete. Run 'otf-browser' from terminal or app menu."
}

# ── detect distro & install ─────────────────────────────────────────────────
TMPDIR="$(mktemp -d)"

if command -v apt-get &>/dev/null; then
  install_deb
elif command -v dnf &>/dev/null; then
  install_rpm
elif command -v pacman &>/dev/null; then
  install_archive
elif command -v zypper &>/dev/null; then
  install_rpm
elif command -v yum &>/dev/null; then
  install_rpm
else
  info "No supported package manager detected. Falling back to AppImage."
  install_appimage
fi

# ── desktop integration hint ────────────────────────────────────────────────
info ""
info "To set OTF Browser as your default browser:"
info "  xdg-settings set default-web-browser otf-browser.desktop"
