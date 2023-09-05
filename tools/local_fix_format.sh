#!/bin/bash
#
# Runs the Envoy format fixers on the current changes. By default, this runs on
# changes that are not yet committed. You can also specify:
#
#   -all: runs on the entire repository (very slow)
#   -main: runs on the changes made from the main branch
#   file1 file2 file3...: runs on the specified files.
#
# To effectively use this script, it's best to run the format fixer before each
# call to 'git commit'.  If you forget to do that then you can run with "-main"
# and it will run on all changes you've made since branching from main, which
# will still be relatively fast.
#
# To run this script, you must provide several environment variables:
#
# CLANG_FORMAT     -- points to the clang formatter binary, perhaps found in
#                     $CLANG_TOOLCHAIN/bin/clang-format
# BUILDIFIER_BIN   -- buildifier location, perhaps found in $HOME/go/bin/buildifier
# BUILDOZER_BIN    -- buildozer location, perhaps found in $HOME/go/bin/buildozer

# If DISPLAY is set, then tkdiff pops up for some BUILD changes.
unset DISPLAY

# The following optional argument is added to be able to run this script using Docker,
# due to a problem to locate clang using WSL on Windows. https://learn.microsoft.com/en-us/windows/wsl/about
# Call with -docker as the first arument.
if [[ $# -gt 0 && "$1" == "-docker" ]]; then
  shift
  exec ./ci/run_envoy_docker.sh "$0" -run-build-setup "$@"
fi

if [[ $# -gt 0 && "$1" == "-run-build-setup" ]]; then
  shift
  . ci/build_setup.sh
fi

if [[ $# -gt 0 && "$1" == "-verbose" ]]; then
  verbose=1
  shift
else
  verbose=0
fi

# Runs the formatting functions on the specified args, echoing commands
# if -vergbose was supplied to the script.
function format_one() {
  (
    if [[ "$verbose" == "1" ]]; then
      set -x
    fi
    bazel run //tools/code_format:check_format -- fix "${1}"
    ./tools/spelling/check_spelling_pedantic.py fix "$1"
  )
}

function format_all() {
  (
    if [[ "$verbose" == "1" ]]; then
      set -x
    fi
    bazel run //tools/code_format:check_format -- fix
    ./tools/spelling/check_spelling_pedantic.py fix
  )
}

if [[ $# -gt 0 && "$1" == "-all" ]]; then
  echo "Checking all files in the repo...this may take a while."
  format_all
else
  if [[ $# -gt 0 && "$1" == "-main" ]]; then
    shift
    echo "Checking all files that have changed since the main branch."
    args=$(git diff main | grep ^diff | awk '{print $3}' | cut -c 3-)
  elif [[ $# == 0 ]]; then
    args=$(git status|grep -E '(modified:|added:)'|awk '{print $2}')
    args+=$(git status|grep -E 'new file:'|awk '{print $3}')
  else
    args="$*"
  fi

  if [[ "$args" == "" ]]; then
    echo No files selected. Bailing out.
    exit 0
  fi
  for arg in $args; do
    format_one "$arg"
  done
fi
