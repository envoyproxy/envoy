#!/bin/bash -e

export NAME=caching-mirror

export PORT_PROXY="${CACHE_MIRROR_PORT_PROXY:-10330}"


# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"
