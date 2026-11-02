#!/bin/sh
# tools/st_audit.sh: the audit of ST's files this firmware is built from.
#
#   sh tools/st_audit.sh check MANIFEST -- SOURCES ARCHIVES -- DEPFILES
#   sh tools/st_audit.sh write MANIFEST -- SOURCES ARCHIVES -- DEPFILES
#
# The files are the ones make fetch placed that the image is built from: the
# ST sources compiled (the startup file among them) and the archives linked,
# given on the command line, and every ST header the compiler opened for any
# object, ST's or ours, which the build's own .d files list. Our files
# (chirp/, ld/, tools/) are not ST's, and -MMD leaves the compiler's own
# headers out of the .d files.
#
# check fails the build when one of those files is not in MANIFEST, when a
# file MANIFEST lists is missing, or when a hash differs. write puts every one
# of them, with its sha256, in MANIFEST: make st-audit-write runs it, and it
# is for use only after a person has audited what changed, for anything that
# arms a receive on the CLI UART (a HAL call, a register write, LL) or echoes
# what it receives.
set -u
mode=$1
manifest=$2
shift 2
[ "${1:-}" = "--" ] && shift
given=""
while [ $# -gt 0 ] && [ "$1" != "--" ]; do
  given="$given $1"
  shift
done
[ "${1:-}" = "--" ] && shift

if command -v sha256sum >/dev/null 2>&1; then
  SUM="sha256sum"
else
  SUM="shasum -a 256"
fi

# Every file: the ones given, and each header a .d file names (its targets,
# the objects, and our own files left out).
needed=$( { for f in $given; do echo "$f"; done
            for d in "$@"; do [ -f "$d" ] && cat "$d"; done | tr ' \t\\' '\n\n\n' | sed 's/:$//' |
              grep -vE '^$|\.o$|^(/|[A-Za-z]:|chirp/|build/|ld/|tools/)'; } | sort -u )

if [ "$mode" = write ]; then
  for f in $needed; do $SUM "$f"; done | sed 's/ \*/  /' > "$manifest" || exit 1
  echo "  wrote $manifest: $(wc -l < "$manifest" | tr -d ' ') ST files, each with its sha256"
  exit 0
fi

[ "$mode" = check ] || { echo "st_audit.sh: check or write, not $mode" >&2; exit 2; }
fail=0
if [ ! -f "$manifest" ]; then
  echo "no $manifest: the ST files this image is built from are not audited"
  exit 1
fi
unlisted=$(printf '%s\n' $needed | awk -v m="$manifest" '
  BEGIN { while ((getline line < m) > 0) { sub(/^[0-9a-f]+  /, "", line); listed[line] = 1 } }
  $0 != "" && !($0 in listed)')
if [ -n "$unlisted" ]; then
  echo "ST files this image is built from that $manifest does not list:"
  echo "$unlisted"
  fail=1
fi
changed=$($SUM -c "$manifest" 2>&1 | grep -v ': OK$')
if [ -n "$changed" ]; then
  echo "ST files $manifest lists that are missing or have changed:"
  echo "$changed"
  fail=1
fi
if [ "$fail" -ne 0 ]; then
  echo "Audit each again (does anything arm a receive on the CLI UART, by a HAL call, a register write or"
  echo "LL, or echo what it receives?), then run make st-audit-write to put the audited files in $manifest."
  exit 1
fi
echo "  OK   the $(printf '%s\n' $needed | wc -l | tr -d ' ') ST files this image is built from are the audited ones"
