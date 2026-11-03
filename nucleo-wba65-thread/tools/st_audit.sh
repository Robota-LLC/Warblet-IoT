#!/bin/sh
# tools/st_audit.sh: the audit of ST's files this firmware is built from, and
# of the options ST's objects are compiled with.
#
#   sh tools/st_audit.sh check MANIFEST FLAGS -- SOURCES ARCHIVES -- DEPFILES -- OPTFILES
#   sh tools/st_audit.sh write MANIFEST FLAGS -- SOURCES ARCHIVES -- DEPFILES -- OPTFILES
#
# The files are the ones make fetch placed that the image is built from: the
# ST sources compiled (the startup file among them) and the archives linked,
# given on the command line, and every ST header the compiler opened for any
# object, ST's or ours, which the build's own .d files list. Our files
# (chirp/, ld/, tools/) are not ST's, and -MMD leaves the compiler's own
# headers out of the .d files.
#
# The options are the build's .opts files for ST's objects (OPTFILES): the
# object's name, then each option it is compiled with, one a line. FLAGS
# records every one of them, in their order, each list once with the objects
# compiled with it: a -D, -U, -include or -std, an include path added or
# moved, a target option, or one handed on to the preprocessor or the
# assembler (-Wp,... -Wa,...), each changes what ST's code compiles to. Only
# what cannot is left out: a warning option with no comma (-Wall, -Wno-...,
# -Werror...) and dependency output (-MD, -MMD, -MP, and -MF, -MT and -MQ
# with their arguments).
#
# check fails the build when one of those files is not in MANIFEST, when a
# file MANIFEST lists is missing, when a hash differs, when a header from
# outside ST's tree has an audited header's name, or when ST's objects are
# compiled with other options than FLAGS records. write puts every one of the
# files, with its sha256, in MANIFEST, and the options in FLAGS: make
# st-audit-write runs it, and it is for use only after a person has audited
# what changed, for anything that arms a receive on the CLI UART (a HAL call,
# a register write, LL) or echoes what it receives.
set -u
mode=$1
manifest=$2
flags=$3
shift 3
[ "${1:-}" = "--" ] && shift
given=""
while [ $# -gt 0 ] && [ "$1" != "--" ]; do
  given="$given $1"
  shift
done
[ "${1:-}" = "--" ] && shift
depfiles=""
while [ $# -gt 0 ] && [ "$1" != "--" ]; do
  depfiles="$depfiles $1"
  shift
done
[ "${1:-}" = "--" ] && shift
optfiles="$*"

if command -v sha256sum >/dev/null 2>&1; then
  SUM="sha256sum"
else
  SUM="shasum -a 256"
fi

# Every file: the ones given, and each header a .d file names (its targets,
# the objects, and our own files left out).
needed=$( { for f in $given; do echo "$f"; done
            for d in $depfiles; do [ -f "$d" ] && cat "$d"; done | tr ' \t\\' '\n\n\n' | sed 's/:$//' |
              grep -vE '^$|\.o$|^(/|[A-Za-z]:|chirp/|build/|ld/|tools/)'; } | sort -u )
# The headers the compiler opened from anywhere else: ours, or outside the tree.
outside=$(for d in $depfiles; do [ -f "$d" ] && cat "$d"; done | tr ' \t\\' '\n\n\n' | sed 's/:$//' |
            grep -E '\.h$' | grep -E '^(/|[A-Za-z]:|chirp/|build/|ld/|tools/)' | sort -u)

# The options, as FLAGS records them: each object's in its .opts file's order,
# warning options with no comma and dependency output left out; each list
# once, then which object is compiled with which.
options() {
  for f in $(printf '%s\n' $optfiles | sort); do
    if [ -f "$f" ]; then printf '@@ '; cat "$f"; else echo "@@ missing $f"; fi
  done | awk '
    function close_object(   key, i) {
      if (object == "") return
      key = ""
      for (i = 1; i <= n; i++) key = key "  " opt[i] "\n"
      if (!(key in set)) { set[key] = ++sets; list[sets] = key }
      objects = objects "  " set[key] " " object "\n"
    }
    /^@@ / { close_object(); object = substr($0, 4); n = 0; skip = 0; next }
    skip { skip = 0; next }
    /^-M[FTQ]$/ { skip = 1; next }
    /^-M[FTQ]./ || /^-(MD|MMD|MP)$/ { next }
    /^-W[^,]*$/ { next }
    { opt[++n] = $0 }
    END {
      close_object()
      print "# The options each ST object this image is built from is compiled with,"
      print "# warnings and dependency output left out: make st-audit-write writes this"
      print "# after a person has audited them (tools/st_audit.sh)."
      for (i = 1; i <= sets; i++) printf "options %d\n%s", i, list[i]
      printf "objects\n%s", objects
    }'
}

if [ "$mode" = write ]; then
  for f in $needed; do $SUM "$f"; done | sed 's/ \*/  /' > "$manifest" || exit 1
  options > "$flags" || exit 1
  echo "  wrote $manifest: $(wc -l < "$manifest" | tr -d ' ') ST files, each with its sha256"
  echo "  wrote $flags: the options of $(printf '%s\n' $optfiles | grep -c .) ST objects"
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
# One of those with an audited ST header's name stands in for it (an include
# path ahead of ST's): what was compiled is not what was audited.
standin=$(printf '%s\n' $outside | awk -v m="$manifest" '
  BEGIN { while ((getline line < m) > 0) { sub(/^[0-9a-f]+  /, "", line); sub(/.*\//, "", line); audited[line] = 1 } }
  { name = $0; sub(/.*\//, "", name) } name != "" && (name in audited)')
if [ -n "$standin" ]; then
  echo "headers from outside ST's tree that stand in for audited ST headers of the same name:"
  echo "$standin"
  fail=1
fi
changed=$($SUM -c "$manifest" 2>&1 | grep -v ': OK$')
if [ -n "$changed" ]; then
  echo "ST files $manifest lists that are missing or have changed:"
  echo "$changed"
  fail=1
fi
if [ ! -f "$flags" ]; then
  echo "no $flags: the options ST's objects are compiled with are not audited"
  fail=1
elif [ "$(options)" != "$(cat "$flags")" ]; then
  echo "ST objects compiled with other options than $flags records (- recorded, + now):"
  options | diff "$flags" - | grep -E '^[<>]' | sed 's/^</-/; s/^>/+/'
  fail=1
fi
if [ "$fail" -ne 0 ]; then
  echo "Audit each again (does anything arm a receive on the CLI UART, by a HAL call, a register write or"
  echo "LL, or echo what it receives?), then run make st-audit-write to put the audited files in $manifest"
  echo "and the options in $flags."
  exit 1
fi
echo "  OK   the $(printf '%s\n' $needed | wc -l | tr -d ' ') ST files this image is built from are the audited ones,"
echo "       and its $(printf '%s\n' $optfiles | grep -c .) ST objects are compiled with the audited options"
