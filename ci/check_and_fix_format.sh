#!/bin/bash

set -e

DIFF_OUTPUT="${DIFF_OUTPUT:-/build/fix_format.diff}"

# We set this for two reasons. First, we want to ensure belt-and-braces that we check these formats
# in CI in case the skip-on-file-change heuristics in proto_format.sh etc. are buggy. Second, this
# prevents AZP cache weirdness.
export FORCE_PROTO_FORMAT=yes
export FORCE_PYTHON_FORMAT=yes

function fix {
  set +e
  ci/do_ci.sh fix_format
  ci/do_ci.sh fix_spelling
  ci/do_ci.sh fix_spelling_pedantic
  echo "Format check failed, try apply following patch to fix:"
  git add api
  git diff HEAD | tee "${DIFF_OUTPUT}"

  exit 1
}

# If any of the checks fail, run the fix function above.
trap fix ERR

ci/do_ci.sh check_format
ci/do_ci.sh check_repositories
ci/do_ci.sh check_spelling
ci/do_ci.sh check_spelling_pedantic
