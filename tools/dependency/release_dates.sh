#!/bin/bash

. tools/shell_utils.sh

set -e

# TODO(phlax): move this job to bazel and remove this
export API_PATH=api/

PYTHONPATH=. python_venv release_dates "$@"
