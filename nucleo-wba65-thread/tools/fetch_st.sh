#!/bin/sh
# tools/fetch_st.sh — the ST sources and libraries this demo builds against.
#
# Nothing from ST is committed here. This script fetches the six repositories
# below at the commits STM32CubeWBA v1.10.0 pins, as blobless, depth-1, sparse
# checkouts: about 100 MB of files where the build reads them, plus about
# 140 MB under .st (git's data, and STM32CubeWBA's own checkout). Git writes
# every file itself, so Windows' path limit is not a problem, and on later runs
# git says whether a placed file is missing or changed and puts it back. A
# repository already here at its commit is not downloaded again. `make fetch`
# runs it; `make` tells you when it is needed. It needs git 2.25 or newer and
# a network connection.
#
# Each repository keeps its own license (see THIRD-PARTY-NOTICES.md at the
# root of this repository); nothing fetched here is covered by the MIT license.

set -u
cd "$(dirname "$0")/.." || exit 1

CACHE=.st
GIT="git -c core.longpaths=true -c core.autocrlf=false -c core.symlinks=false -c diff.ignoreSubmodules=all"
FRESH=1

fail() {
  echo "fetch_st.sh: $*" >&2
  exit 1
}

# fetch NAME URL SHA TREE [SPARSE DIR...]
# A sparse (cone) checkout of SHA with git directory .st/NAME.git and work
# tree TREE. Sets FRESH=0 when it fetched, FRESH=1 when the repository was
# already here at SHA. Any failure stops the script.
fetch() {
  name=$1; url=$2; sha=$3; tree=$4; shift 4
  gd="$CACHE/$name.git"
  g="$GIT --git-dir=$gd --work-tree=$tree"
  if [ -d "$gd" ] && [ "$($g rev-parse HEAD 2>/dev/null)" = "$sha" ]; then
    echo "  ok     $name is at $sha"
    FRESH=1
    return
  fi
  echo "  fetch  $name  $url  @ $sha"
  rm -rf "$gd"
  mkdir -p "$tree" "$CACHE" || fail "$name: cannot create $tree"
  $g init -q || fail "$name: git init failed"
  # ST's own .gitattributes must not rewrite line endings: what is written is
  # the blob, and what is compared later is the blob.
  mkdir -p "$gd/info" && echo "* -text" > "$gd/info/attributes" || fail "$name: cannot write attributes"
  $g remote add origin "$url" || fail "$name: git remote add failed"
  if [ "$#" -gt 0 ]; then
    $g sparse-checkout set --cone "$@" || fail "$name: sparse-checkout failed"
  fi
  $g fetch -q --depth 1 --filter=blob:none origin "$sha" || fail "$name: fetching $sha from $url failed"
  $g checkout -q --detach FETCH_HEAD || fail "$name: checkout into $tree failed"
  got=$($g rev-parse HEAD) || fail "$name: no commit after checkout"
  [ "$got" = "$sha" ] || fail "$name: fetched $got, expected $sha"
  FRESH=0
}

# keep NAME TREE — TREE is NAME's own work tree: after a fresh fetch there is
# nothing to do; otherwise ask git what is missing or changed and restore it.
keep() {
  g="$GIT --git-dir=$CACHE/$1.git --work-tree=$2"
  [ "$FRESH" = 0 ] && return
  changed=$($g status --porcelain --untracked-files=no 2>/dev/null)
  if [ $? -ne 0 ] || [ -n "$changed" ]; then     # a failed status is a missing tree
    echo "  redo   $2 has files missing or changed; restoring them"
    mkdir -p "$2" || fail "$1: cannot create $2"
    $g checkout -q -- . || fail "$1: restoring $2 failed"
  fi
}

# place NAME PATH... — write NAME's checked-out files under PATHs into this
# directory, at the same paths, through git: a checkout of just those paths
# with this directory as the work tree, so nothing else of NAME's lands here.
# On later runs only when git finds one missing or changed.
place() {
  name=$1; shift
  g="$GIT --git-dir=$CACHE/$name.git --work-tree=."
  if [ "$FRESH" = 1 ]; then
    changed=$($g diff-index --name-only HEAD -- "$@" 2>/dev/null)
    if [ $? -eq 0 ] && [ -z "$changed" ]; then
      return
    fi
    echo "  redo   $name's files here are missing or changed; restoring them"
  fi
  $g checkout -q -- "$@" || fail "$name: writing its files here failed"
  echo "  place  $*"
}

# STM32CubeWBA v1.10.0: the board application, the shared WPAN project code,
# the utilities, the CMSIS core headers and their license. Checked out under
# .st/cube, and those paths placed here at the same paths.
CUBE_PATHS="Projects/NUCLEO-WBA65RI/Applications/Thread/Thread_Cli_Cmd_FTD Projects/Common Utilities Drivers/CMSIS/Include Drivers/CMSIS/Core Drivers/CMSIS/LICENSE.txt"
fetch cube https://github.com/STMicroelectronics/STM32CubeWBA.git \
      e455b860ceb52aaa0332a98f1e7a10bbd7014fc9 "$CACHE/cube" \
      Projects/NUCLEO-WBA65RI/Applications/Thread/Thread_Cli_Cmd_FTD \
      Projects/Common Utilities Drivers/CMSIS/Include Drivers/CMSIS/Core
# shellcheck disable=SC2086
place cube $CUBE_PATHS

# The WPAN middleware: ST's OpenThread port and the prebuilt OpenThread stack,
# the 802.15.4 link layer, the MAC. Checked out where the build reads it.
fetch wpan https://github.com/STMicroelectronics/stm32-mw-wpan.git \
      7e764835982f0a8b94fdd6b2b056d6e0afc4d1ba Middlewares/ST/STM32_WPAN \
      thread/openthread link_layer mac_802_15_4
keep wpan Middlewares/ST/STM32_WPAN

# The HAL and LL drivers (the whole repository).
fetch hal https://github.com/STMicroelectronics/stm32wbaxx_hal_driver.git \
      c36d9aabc6068051ed27a3a683a6b4f35f04d815 Drivers/STM32WBAxx_HAL_Driver
keep hal Drivers/STM32WBAxx_HAL_Driver

# The device headers and startup sources.
fetch cmsisd https://github.com/STMicroelectronics/cmsis_device_wba.git \
      3373b40fa3b71ce114f58b170acd0f8833d42508 Drivers/CMSIS/Device/ST/STM32WBAxx \
      Include Source
keep cmsisd Drivers/CMSIS/Device/ST/STM32WBAxx

# The Nucleo board support package (the whole repository).
fetch bsp https://github.com/STMicroelectronics/stm32wbaxx-nucleo-bsp.git \
      653af5460b5fac428f4174dba6144518cf0a73a0 Drivers/BSP/STM32WBAxx_Nucleo
keep bsp Drivers/BSP/STM32WBAxx_Nucleo

# mbedTLS headers; the library itself is linked from the WPAN prebuilt archive.
fetch mbed https://github.com/STMicroelectronics/stm32-mw-mbedtls.git \
      7ee7e20ec1460857446d2fa5628c1fd5ce6cd411 Middlewares/Third_Party/mbedtls \
      include
keep mbed Middlewares/Third_Party/mbedtls

echo "ST sources are in place. Optional: rm -rf $CACHE frees about 140 MB; the next 'make fetch' then downloads again."
