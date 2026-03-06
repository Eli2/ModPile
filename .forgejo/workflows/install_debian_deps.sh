#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
README="$SCRIPT_DIR/../../README.md"

# Extract packages from the ```sh block containing "sudo apt install"
packages=$(awk '
    /^```sh/         { in_block=1; next }
    in_block && /^```/ { in_block=0; found=0; next }
    in_block && /sudo apt install/ { found=1; next }
    in_block && found {
        # strip leading whitespace, trailing backslash, skip empty lines
        gsub(/^[[:space:]]+/, "")
        gsub(/[[:space:]]*\\[[:space:]]*$/, "")
        if ($0 != "") print $0
    }
' "$README")

if [ -z "$packages" ]; then
    echo "ERROR: No packages found in README.md" >&2
    exit 1
fi

echo "Packages to install:"
echo "$packages" | while read -r pkg; do
    echo "  - $pkg"
done
echo

echo "$packages" | while read -r pkg; do
    echo ">>> apt-get install -y $pkg"
    apt-get install -y "$pkg"
done
