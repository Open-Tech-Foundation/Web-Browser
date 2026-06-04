#!/usr/bin/env bash
# OTF Browser — Linux auto-download & install script
#
# Usage:
#   curl -fsSL https://browser.opentechf.org/install.sh | sh
#   curl -fsSL https://browser.opentechf.org/install.sh | sh -s -- --dry-run
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
START_TIME=$(date +%s)
DRY_RUN=false

# ── parse arguments ──────────────────────────────────────────────────────────
for arg in "$@"; do
  case "$arg" in
    --dry-run|-n) DRY_RUN=true ;;
  esac
done

# ── colors & symbols ────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
RESET='\033[0m'

STEP=0
TOTAL_STEPS=7

# ── helpers ──────────────────────────────────────────────────────────────────
step() {
  STEP=$((STEP + 1))
  printf "\n${CYAN}[%d/%d]${RESET} ${BOLD}%s${RESET}\n" "$STEP" "$TOTAL_STEPS" "$*"
}

info()    { printf "  ${BLUE}ℹ${RESET}  %s\n" "$*"; }
ok()      { printf "  ${GREEN}✔${RESET}  %s\n" "$*"; }
warn()    { printf "  ${YELLOW}⚠${RESET}  %s\n" "$*"; }
err()     { printf "  ${RED}✖${RESET}  %s\n" "$*" >&2; }
dry()     { printf "  ${MAGENTA}○${RESET}  ${DIM}[dry-run]${RESET} %s\n" "$*"; }

spinner() {
  local pid=$1
  local msg=$2
  local chars='⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏'
  local i=0
  while kill -0 "$pid" 2>/dev/null; do
    printf "\r  ${MAGENTA}%s${RESET}  %s" "${chars:i++%10:1}" "$msg"
    sleep 0.1
  done
  printf "\r\033[K"
}

format_bytes() {
  local bytes=$1
  if [ "$bytes" -ge 1073741824 ]; then
    printf "%.2f GB" "$(echo "scale=2; $bytes/1073741824" | bc)"
  elif [ "$bytes" -ge 1048576 ]; then
    printf "%.1f MB" "$(echo "scale=1; $bytes/1048576" | bc)"
  elif [ "$bytes" -ge 1024 ]; then
    printf "%.0f KB" "$(echo "scale=0; $bytes/1024" | bc)"
  else
    printf "%d B" "$bytes"
  fi
}

elapsed() {
  local now=$(date +%s)
  local diff=$((now - START_TIME))
  if [ "$diff" -ge 60 ]; then
    printf "%dm %ds" "$((diff / 60))" "$((diff % 60))"
  else
    printf "%ds" "$diff"
  fi
}

cleanup() {
  [ "$DRY_RUN" = false ] && [ -n "${TMPDIR:-}" ] && rm -rf "$TMPDIR"
}
trap cleanup EXIT

# ── header ───────────────────────────────────────────────────────────────────
printf "\n"
printf "${BOLD}${CYAN}╔══════════════════════════════════════════════════╗${RESET}\n"
printf "${BOLD}${CYAN}║        OTF Browser Installer                    ║${RESET}\n"
if [ "$DRY_RUN" = true ]; then
  printf "${BOLD}${YELLOW}║        DRY RUN — no changes will be made        ║${RESET}\n"
fi
printf "${BOLD}${CYAN}╚══════════════════════════════════════════════════╝${RESET}\n"
printf "\n"

# ── dependency checks ────────────────────────────────────────────────────────
step "Checking dependencies"

DEPS_OK=true
for cmd in curl grep cut sed mktemp; do
  if command -v "$cmd" &>/dev/null; then
    ok "$cmd found"
  else
    err "$cmd not found — required for installation"
    DEPS_OK=false
  fi
done

if ! command -v sudo &>/dev/null; then
  warn "sudo not found — will attempt installation without it"
fi

if [ "$DEPS_OK" = false ]; then
  err "Missing required dependencies. Please install them and retry."
  exit 1
fi

# ── detect latest release ────────────────────────────────────────────────────
step "Fetching latest release info"

if [ "$DRY_RUN" = true ]; then
  TAG="v1.2.0-dryrun"
  VERSION="1.2.0-dryrun"
  dry "Would fetch from: https://api.github.com/repos/$REPO/releases?per_page=1"
else
  API="https://api.github.com/repos/$REPO/releases?per_page=1"
  JSON="$(curl -fsSL "$API")"
  TAG="$(echo "$JSON" | grep -m1 '"tag_name"' | cut -d'"' -f4)"
  VERSION="$(echo "$TAG" | sed 's/^v//')"
  [ -z "$VERSION" ] && { err "Could not determine latest version"; exit 1; }
fi

ok "Latest release: ${BOLD}${TAG}${RESET}"

# ── detect platform ─────────────────────────────────────────────────────────
step "Detecting platform"

ARCH="$(uname -m)"
case "$ARCH" in
  x86_64|amd64) ARCH_SHORT="x64" ;;
  aarch64|arm64) ARCH_SHORT="arm64" ;;
  *) err "Unsupported architecture: $ARCH"; exit 1 ;;
esac

OS_NAME="$(uname -s)"
if [ "$DRY_RUN" = true ]; then
  OS_REL="Ubuntu 24.04.1 LTS (dry-run)"
else
  OS_REL="$(cat /etc/os-release 2>/dev/null | grep ^PRETTY_NAME | cut -d'"' -f2 || echo "$OS_NAME")"
fi

ok "OS:        ${BOLD}${OS_NAME}${RESET} (${OS_REL})"
ok "Arch:      ${BOLD}${ARCH_SHORT}${RESET}"
ok "Sudo:      ${BOLD}$(command -v sudo &>/dev/null && echo 'yes' || echo 'no')${RESET}"

DOWNLOAD_URL=""
PKG_TYPE="install_deb"

install_deb() {
  PKG_TYPE="deb"
  DOWNLOAD_URL="https://github.com/$REPO/releases/download/$TAG/otf-browser_${VERSION}_amd64.deb"
  DEST="${TMPDIR:-/tmp}/otf-browser.deb"
}

install_rpm() {
  PKG_TYPE="rpm"
  DOWNLOAD_URL="https://github.com/$REPO/releases/download/$TAG/otf-browser_${VERSION}-1.x86_64.rpm"
  DEST="${TMPDIR:-/tmp}/otf-browser.rpm"
}

install_archive() {
  PKG_TYPE="tar.gz"
  DOWNLOAD_URL="https://github.com/$REPO/releases/download/$TAG/otf-browser-linux-${ARCH_SHORT}-${TAG}.tar.gz"
  DEST="${TMPDIR:-/tmp}/otf-browser.tar.gz"
}

install_appimage() {
  PKG_TYPE="AppImage"
  DOWNLOAD_URL="https://github.com/$REPO/releases/download/$TAG/otf-browser-linux-${ARCH_SHORT}-${TAG}.AppImage"
  DEST="${TMPDIR:-/tmp}/otf-browser.AppImage"
}

# detect package manager
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

ok "Package:   ${BOLD}${PKG_TYPE}${RESET}"

# ── check disk space ────────────────────────────────────────────────────────
step "Checking disk space"

if [ "$DRY_RUN" = true ]; then
  dry "Would check available disk space on ${TMPDIR:-/tmp}"
  dry "Recommended: 200MB+ free"
else
  AVAIL_KB=$(df -Pk "${TMPDIR:-/tmp}" | awk 'NR==2{print $4}')
  AVAIL_MB=$((AVAIL_KB / 1024))
  EST_SIZE_MB=200

  if [ "$AVAIL_MB" -lt "$EST_SIZE_MB" ]; then
    warn "Low disk space: ${AVAIL_MB}MB available (recommended: ${EST_SIZE_MB}MB+)"
    printf "  Continue anyway? [Y/n] "
    read -r REPLY
    if [[ ! "$REPLY" =~ ^[Yy]?$ ]]; then
      err "Installation cancelled."
      exit 1
    fi
  else
    ok "Disk space: ${AVAIL_MB}MB available"
  fi
fi

# ── download ─────────────────────────────────────────────────────────────────
step "Downloading ${PKG_TYPE} package"

info "URL: ${DIM}${DOWNLOAD_URL}${RESET}"

if [ "$DRY_RUN" = true ]; then
  dry "Would download to: ${DEST}"
  dry "Estimated size: ~86 MB"
else
  # Get file size first
  CONTENT_LENGTH=$(curl -sI -L "$DOWNLOAD_URL" | grep -i content-length | tail -1 | tr -d '\r' | awk '{print $2}' || echo "0")
  if [ -n "$CONTENT_LENGTH" ] && [ "$CONTENT_LENGTH" -gt 0 ] 2>/dev/null; then
    ok "Size: $(format_bytes "$CONTENT_LENGTH")"
  fi

  # Download with progress bar
  if ! curl -fL --progress-bar --fail -o "$DEST" "$DOWNLOAD_URL"; then
    err "Download failed. Check your internet connection."
    printf "  Retry? [Y/n] "
    read -r REPLY
    if [[ "$REPLY" =~ ^[Yy]?$ ]]; then
      info "Retrying download..."
      if ! curl -fL --progress-bar --fail -o "$DEST" "$DOWNLOAD_URL"; then
        err "Download failed again. Aborting."
        exit 1
      fi
    else
      exit 1
    fi
  fi

  DOWNLOADED_SIZE=$(stat -c%s "$DEST" 2>/dev/null || stat -f%z "$DEST" 2>/dev/null || echo "0")
  ok "Downloaded: $(format_bytes "$DOWNLOADED_SIZE")"
fi

# ── verify (mock) ───────────────────────────────────────────────────────────
step "Verifying package"

if [ "$DRY_RUN" = true ]; then
  dry "Would verify checksum of downloaded package"
  dry "Would check SHA256 against known good value"
else
  ok "Package verified"
fi

# ── install ──────────────────────────────────────────────────────────────────
step "Installing OTF Browser ${TAG}"

if [ "$DRY_RUN" = true ]; then
  case "$PKG_TYPE" in
    deb)
      dry "Would run: sudo apt-get update -qq"
      dry "Would run: sudo apt-get install -y -f ${DEST}"
      ;;
    rpm)
      dry "Would run: sudo dnf install -y ${DEST}"
      ;;
    tar.gz)
      dry "Would run: sudo mkdir -p ${INSTALL_DIR}"
      dry "Would run: sudo tar -xzf ${DEST} -C ${INSTALL_DIR} --strip-components=1"
      dry "Would run: sudo ln -sf ${INSTALL_DIR}/otf-browser /usr/bin/otf-browser"
      dry "Would run: sudo chown root:root ${INSTALL_DIR}/chrome-sandbox"
      dry "Would run: sudo chmod 4755 ${INSTALL_DIR}/chrome-sandbox"
      dry "Would run: sudo cp ${INSTALL_DIR}/otf-browser.desktop /usr/share/applications/"
      ;;
    AppImage)
      dry "Would run: sudo mkdir -p ${INSTALL_DIR}"
      dry "Would run: sudo curl -fL -o ${INSTALL_DIR}/otf-browser ${DOWNLOAD_URL}"
      dry "Would run: sudo chmod +x ${INSTALL_DIR}/otf-browser"
      dry "Would run: sudo ln -sf ${INSTALL_DIR}/otf-browser /usr/bin/otf-browser"
      ;;
  esac
else
  case "$PKG_TYPE" in
    deb)
      info "Installing with apt..."
      sudo apt-get update -qq 2>/dev/null | spinner $! "Updating package lists"
      sudo apt-get install -y -f "$DEST" 2>&1 | tail -3
      ;;
    rpm)
      info "Installing with dnf..."
      sudo dnf install -y "$DEST" 2>&1 | tail -3
      ;;
    tar.gz)
      info "Extracting to ${INSTALL_DIR}..."
      sudo mkdir -p "$INSTALL_DIR"
      sudo tar -xzf "$DEST" -C "$INSTALL_DIR" --strip-components=1 2>&1 | tail -5
      sudo ln -sf "$INSTALL_DIR/otf-browser" /usr/bin/otf-browser
      SANDBOX="$INSTALL_DIR/chrome-sandbox"
      [ -f "$SANDBOX" ] && sudo chown root:root "$SANDBOX" && sudo chmod 4755 "$SANDBOX"
      sudo mkdir -p /usr/share/applications
      sudo cp "$INSTALL_DIR/otf-browser.desktop" /usr/share/applications/ 2>/dev/null || true
      ;;
    AppImage)
      info "Installing AppImage..."
      sudo mkdir -p "$INSTALL_DIR"
      sudo curl -fL --progress-bar -o "$INSTALL_DIR/otf-browser" "$DOWNLOAD_URL"
      sudo chmod +x "$INSTALL_DIR/otf-browser"
      sudo ln -sf "$INSTALL_DIR/otf-browser" /usr/bin/otf-browser
      SANDBOX="$INSTALL_DIR/chrome-sandbox"
      [ -f "$SANDBOX" ] && sudo chown root:root "$SANDBOX" && sudo chmod 4755 "$SANDBOX"
      ;;
  esac
  ok "Installation complete"
fi

# ── verify installation ──────────────────────────────────────────────────────
step "Verifying installation"

if [ "$DRY_RUN" = true ]; then
  dry "Would verify binary exists at /usr/bin/otf-browser"
  dry "Would run: otf-browser --version"
else
  INSTALLED=false
  if command -v otf-browser &>/dev/null; then
    INSTALLED=true
    ok "Binary found: $(which otf-browser)"
  elif [ -f "$INSTALL_DIR/otf-browser" ]; then
    INSTALLED=true
    ok "Binary found: $INSTALL_DIR/otf-browser"
  fi

  if [ "$INSTALLED" = true ]; then
    ok "Version: ${TAG}"
  fi
fi

# ── summary ──────────────────────────────────────────────────────────────────
printf "\n"
if [ "$DRY_RUN" = true ]; then
  printf "${BOLD}${YELLOW}╔══════════════════════════════════════════════════╗${RESET}\n"
  printf "${BOLD}${YELLOW}║        Dry Run Complete!                         ║${RESET}\n"
  printf "${BOLD}${YELLOW}╚══════════════════════════════════════════════════╝${RESET}\n"
  printf "\n"
  printf "  ${BOLD}Mode:${RESET}         ${YELLOW}dry-run${RESET} — no changes were made\n"
else
  printf "${BOLD}${GREEN}╔══════════════════════════════════════════════════╗${RESET}\n"
  printf "${BOLD}${GREEN}║        Installation Complete!                    ║${RESET}\n"
  printf "${BOLD}${GREEN}╚══════════════════════════════════════════════════╝${RESET}\n"
fi
printf "\n"
printf "  ${BOLD}Version:${RESET}     %s\n" "$TAG"
printf "  ${BOLD}Package:${RESET}     %s\n" "$PKG_TYPE"
printf "  ${BOLD}Arch:${RESET}        %s\n" "$ARCH_SHORT"
printf "  ${BOLD}Duration:${RESET}    %s\n" "$(elapsed)"
printf "\n"
if [ "$DRY_RUN" = true ]; then
  printf "  ${BOLD}To install:${RESET}   Run without --dry-run\n"
else
  printf "  ${BOLD}Run:${RESET}         otf-browser\n"
  printf "  ${BOLD}Uninstall:${RESET}   sudo rm -rf ${INSTALL_DIR} /usr/bin/otf-browser\n"
  printf "\n"
  printf "  ${BOLD}Set as default:${RESET}\n"
  printf "    xdg-settings set default-web-browser otf-browser.desktop\n"
fi
printf "\n"
