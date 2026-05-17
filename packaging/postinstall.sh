#!/bin/sh
# Run as root by apt/dpkg/yum/dnf after the package files are extracted.
# The Chromium sandbox helper must be SUID-root to drop into its restricted
# child; without this the browser aborts on startup unless launched with
# --no-sandbox.
set -e
SANDBOX=/opt/otf-browser/chrome-sandbox
if [ -f "$SANDBOX" ]; then
    chown root:root "$SANDBOX"
    chmod 4755 "$SANDBOX"
fi
exit 0
