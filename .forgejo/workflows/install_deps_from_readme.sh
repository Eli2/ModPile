#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
README="$SCRIPT_DIR/../../README.md"

# Parse README: for each ```sh block containing "sudo <cmd> ...",
# emit one line: "<cmd> <pkg1> <pkg2> ..."
readme_installs=$(awk '
    /^```sh/           { in_block=1; cmd=""; pkgs=""; next }
    in_block && /^```/ { if (cmd != "" && pkgs != "") print cmd pkgs
                         in_block=0; next }
    in_block && cmd=="" && /^sudo [a-z]/ { cmd = $2; next }
    in_block && cmd!="" {
        gsub(/^[[:space:]]+/, "")
        gsub(/[[:space:]]*\\[[:space:]]*$/, "")
        if ($0 != "") pkgs = pkgs " " $0
    }
' "$README")

# Find the first block whose package manager binary is available on this system
pm_binary=""
packages=""
while IFS= read -r entry; do
    binary="${entry%% *}"
    if command -v "$binary" &>/dev/null; then
        pm_binary="$binary"
        packages="${entry#* }"
        break
    fi
done <<< "$readme_installs"

if [ -z "$pm_binary" ]; then
    echo "ERROR: No supported package manager found on this system" >&2
    exit 1
fi

echo "Package manager: $pm_binary"
echo "Packages to install:"
for pkg in $packages; do
    echo "  - $pkg"
done
echo

case "$pm_binary" in
    apt)
        # shellcheck disable=SC2086
        DEBIAN_FRONTEND=noninteractive apt-get install -y $packages
        ;;
    pacman)
        # shellcheck disable=SC2086
        pacman -S --noconfirm --needed $packages
        ;;
    dnf)
        # shellcheck disable=SC2086
        dnf install -y $packages
        ;;
    *)
        echo "ERROR: Don't know how to invoke '$pm_binary' non-interactively" >&2
        exit 1
        ;;
esac
