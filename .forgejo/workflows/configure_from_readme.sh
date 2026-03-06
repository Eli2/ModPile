#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
README="$SCRIPT_DIR/../../README.md"

# The section to search for can be passed as an argument (e.g. "arch", "fedora",
# "debian"). The string is matched case-insensitively against ### headings in the
# README (e.g. "arch" matches "### Arch Linux"). When no argument is given the
# distro ID from /etc/os-release is used as the search string.
if [ $# -ge 1 ]; then
    section="$1"
else
    if [ ! -f /etc/os-release ]; then
        echo "ERROR: /etc/os-release not found and no distro argument given" >&2
        exit 1
    fi
    . /etc/os-release
    section="${ID:-}"
fi

if [ -z "$section" ]; then
    echo "ERROR: Could not determine distro; pass it as an argument (e.g. ubuntu, arch, fedora)" >&2
    exit 1
fi

echo "Section: $section"
echo

# Extract the cmake -B command from the matching distro section.
# Multi-line commands (continuation lines ending with \) are joined into one.
#
# State variables:
#   in_section  — true while we are inside the target ### heading
#   in_block    — true while we are inside a ```sh ... ``` fence
#   cmd         — cmake command being assembled; empty until we find "cmake -B"
cmake_cmd=$(awk -v section="$section" '
    # --- Section tracking ---
    # A level-3 heading (###) marks the start of a distro section.
    # Match case-insensitively: "arch" matches "### Arch Linux", etc.
    /^### / {
        heading = tolower($0)
        in_section = index(heading, tolower(section)) > 0
        in_block = 0
        cmd = ""
        next
    }
    # A level-2 heading (## but not ###) ends all distro sections.
    /^## [^#]/ {
        in_section = 0
        in_block = 0
        cmd = ""
        next
    }
    # Skip every line that is not inside our target section.
    !in_section { next }

    # --- Code fence tracking ---
    # Opening fence: enter block mode.
    /^```sh/ { in_block = 1; next }
    # Closing fence: leave block mode and reset any partial command
    # (this ```sh block did not contain cmake -B, so discard it).
    in_block && /^```/ { in_block = 0; cmd = ""; next }

    # --- cmake -B command assembly ---
    # First line of a cmake block: only start collecting when the
    # first line of the fence is exactly "cmake -B ...".
    in_block && cmd == "" && /^cmake -B/ {
        line = $0
        gsub(/[[:space:]]*\\[[:space:]]*$/, "", line)  # strip trailing backslash
        cmd = line
        # If this line has no trailing \, the command is complete.
        if ($0 !~ /\\[[:space:]]*$/) { print cmd; exit }
        next
    }
    # Continuation lines: append to cmd until a line has no trailing \.
    in_block && cmd != "" {
        line = $0
        gsub(/^[[:space:]]+/,              "", line)   # strip leading indent
        gsub(/[[:space:]]*\\[[:space:]]*$/, "", line)   # strip trailing backslash
        cmd = cmd " " line
        if ($0 !~ /\\[[:space:]]*$/) { print cmd; exit }
    }
' "$README")

if [ -z "$cmake_cmd" ]; then
    echo "ERROR: No 'cmake -B' block found in a README ### heading matching '$section'" >&2
    exit 1
fi

echo "Running: $cmake_cmd"
echo
eval "$cmake_cmd"
