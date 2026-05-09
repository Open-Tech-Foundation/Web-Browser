#!/bin/bash

# Dependency Setup Script for OTF Browser
# This script downloads and extracts the required CEF version.

set -e

CEF_VERSION=$(cat CEF_VERSION)
CEF_PLATFORM="linux64"
CEF_DIST="cef_binary_${CEF_VERSION}_${CEF_PLATFORM}"
CEF_ARCHIVE="${CEF_DIST}.tar.bz2"
# URL encode the '+' signs in the version
CEF_URL_VERSION=$(echo $CEF_VERSION | sed 's/+/%%2B/g')
CEF_DOWNLOAD_URL="https://cef-builds.spotifycdn.com/cef_binary_${CEF_URL_VERSION}_${CEF_PLATFORM}.tar.bz2"

THIRD_PARTY_DIR="third_party"
CEF_TARGET_DIR="${THIRD_PARTY_DIR}/cef"

echo "Setting up dependencies..."
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

echo "Extracting CEF..."
tar -xjf "${CEF_ARCHIVE}"

echo "Moving CEF to ${CEF_TARGET_DIR}..."
mv "${CEF_DIST}" "${CEF_TARGET_DIR}"

echo "Cleaning up..."
rm "${CEF_ARCHIVE}"

echo "Dependencies set up successfully!"
