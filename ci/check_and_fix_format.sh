#!/bin/bash

set -e

function fix {
  set +e
  ci/do_ci.sh fix_format
  ci/do_ci.sh fix_spelling
  ci/do_ci.sh fix_spelling_pedantic
  echo "Format check faild, try apply following patch to fix:"
  git diff HEAD | tee /build/fix_format.diff

  exit 1
}

trap fix ERR

ci/do_ci.sh check_format
ci/do_ci.sh check_repositories
ci/do_ci.sh check_spelling
ci/do_ci.sh check_spelling_pedantic
