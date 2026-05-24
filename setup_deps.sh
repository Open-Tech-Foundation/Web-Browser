#!/bin/bash

# Dependency Setup Script for OTF Browser
# This script downloads and extracts the required CEF version.
#
# Usage: ./setup_deps.sh [platform]
#   platform - target platform: linux64 (default) or windows64

set -e

CEF_VERSION=$(cat CEF_VERSION)
CEF_PLATFORM="${1:-linux64}"
# Use the "minimal" distribution — drops Debug/ binaries and the
# cefclient/cefsimple sample-app sources we don't build. About half the
# size of the standard distribution (~300 MB vs ~640 MB), which matters
# for CI cache size and cold-start CDN fetch time.
CEF_DIST="cef_binary_${CEF_VERSION}_${CEF_PLATFORM}_minimal"
CEF_ARCHIVE="${CEF_DIST}.tar.bz2"
# URL-encode the '+' signs in the version. sed doesn't interpret %% as a
# percent escape (that's printf), so the previous "%%2B" wrote a literal
# double percent into the URL and Spotify's CDN returned 404.
CEF_URL_VERSION=$(echo "$CEF_VERSION" | sed 's/+/%2B/g')
CEF_DOWNLOAD_URL="https://cef-builds.spotifycdn.com/cef_binary_${CEF_URL_VERSION}_${CEF_PLATFORM}_minimal.tar.bz2"

THIRD_PARTY_DIR="third_party"
CEF_TARGET_DIR="${THIRD_PARTY_DIR}/cef"

# Pick the platform-appropriate SHA256 verification file
SHA256_FILE="CEF_SHA256"
if [ "${CEF_PLATFORM}" = "windows64" ]; then
    SHA256_FILE="CEF_SHA256_WIN64"
fi

echo "Setting up dependencies for ${CEF_PLATFORM}..."
mkdir -p "${THIRD_PARTY_DIR}"

if [ -d "${CEF_TARGET_DIR}" ]; then
    echo "CEF directory already exists at ${CEF_TARGET_DIR}. Skipping download."
    echo "To re-download, delete the directory and run this script again."
    exit 0
fi

echo "Downloading CEF version ${CEF_VERSION}..."
echo "URL: ${CEF_DOWNLOAD_URL}"

# Use curl to download
if ! curl -fLo "${CEF_ARCHIVE}" "${CEF_DOWNLOAD_URL}"; then
    echo "Error: Failed to download CEF. Please check your internet connection or the version string."
    exit 1
fi

# Verify the archive matches the SHA256 pinned in the repo before doing
# anything with it. Without this, a compromised CDN (or TLS MITM) could
# silently swap a backdoored CEF/Chromium into the release build.
if [ ! -f "${SHA256_FILE}" ]; then
    echo "Error: ${SHA256_FILE} file is missing. Refusing to extract unverified CEF archive." >&2
    rm -f "${CEF_ARCHIVE}"
    exit 1
fi
EXPECTED_SHA256=$(tr -d '[:space:]' < "${SHA256_FILE}")
echo "Verifying CEF archive checksum..."
if ! echo "${EXPECTED_SHA256}  ${CEF_ARCHIVE}" | sha256sum -c -; then
    echo "Error: CEF archive checksum mismatch. Refusing to extract." >&2
    echo "  Expected: ${EXPECTED_SHA256}" >&2
    echo "  Actual:   $(sha256sum "${CEF_ARCHIVE}" | awk '{print $1}')" >&2
    rm -f "${CEF_ARCHIVE}"
    exit 1
fi

echo "Extracting CEF..."
tar -xjf "${CEF_ARCHIVE}"

echo "Moving CEF to ${CEF_TARGET_DIR}..."
mv "${CEF_DIST}" "${CEF_TARGET_DIR}"

echo "Cleaning up..."
rm "${CEF_ARCHIVE}"

echo "Dependencies set up successfully!"
