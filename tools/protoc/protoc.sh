#!/bin/bash -e

set -o pipefail

"$@" |& grep -v "warning: directory does not exist" || :
