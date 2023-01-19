#!/bin/sh
#
# Runs the Enovy format fixers on the current changes. By default, this runs on
# changes that are not yet committed. You can also specify:
#
#   -all: runs on the entire repository (very slow)
#   -main: runs on the changes amde from the main branch
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

# Trigger an error if the required env vars are not set.
set -u
ignore="$CLANG_FORMAT $BUILDIFIER_BIN $BUILDOZER_BIN"

if [[ $# > 0 && "$1" == "-all" ]]; then
  all=1
  echo "Checking all files in the repo...this may take a while."
  shift
  args="$@"
else
  if [[ $# > 0 && "$1" == "-main" ]]; then
    shift
    echo "Checking all files that have changed since the main branch."
    args=$(git diff main | grep ^diff | awk '{print $3}' | cut -c 3-)
  elif [[ $# == 0 ]]; then
    args=$(git status|egrep '(modified:|added:)'|awk '{print $2}')
    args+=$(git status|egrep 'new file:'|awk '{print $3}')
  else
    args="$@"
  fi

  if [[ "$args" == "" ]]; then
    echo No files selected. Bailing out.
    exit 0
  fi
fi

echo ./tools/code_format/check_format.py fix $args '|& grep -v "no longer checks API"'
./tools/code_format/check_format.py fix $args |& grep -v "no longer checks API"
echo ./tools/spelling/check_spelling_pedantic.py fix $args
./tools/spelling/check_spelling_pedantic.py fix $args
