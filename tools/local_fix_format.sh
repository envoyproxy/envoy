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

# If DISPLAY is set, then tkdiff pops up for some BUILD changes.
unset DISPLAY

if [ "$1" = "-all" ]; then
  all=1
  echo ALL.
  shift
  args="$@"
else
  if [ "$1" = "-main" ]; then
    shift
    args=$(git diff main | grep ^diff | awk '{print $3}' | cut -c 3-)
  elif [ -z "$1" ]; then
    args=$(git status|egrep '(modified:|added:)'|awk '{print $2}')
    args+=$(git status|egrep 'new file:'|awk '{print $3}')
  else
    args="$@"
  fi
  for arg in $args; do
    echo ./tools/code_format/check_format.py fix $arg
    ./tools/code_format/check_format.py fix $arg |& grep -v "no longer checks API"
    echo ./tools/spelling/check_spelling_pedantic.py fix $arg
    ./tools/spelling/check_spelling_pedantic.py fix $arg
  done
  exit $?
fi

./tools/code_format/check_format.py fix $args  |& grep -v "no longer checks API"
./tools/spelling/check_spelling_pedantic.py fix $args
