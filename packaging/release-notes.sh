#!/bin/sh
# Prints the top debian/changelog entry as Markdown, for the release notes.
# Usage: release-notes.sh
set -eu

notes=$(awk '
NR == 1 { next }
/^ -- / { exit }
/^  \* / { if (item != "") print item; item = "- " substr($0, 5); next }
/^    / { sub(/^ +/, ""); item = item " " $0; next }
END { if (item != "") print item }
' "$(dirname "$0")/debian/changelog")
[ -n "$notes" ] || { echo "FATAL: the top debian/changelog entry has no items" >&2; exit 1; }
printf '%s\n' "$notes"
