#!/bin/bash -e

export NAME=dynamic-configuration

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

# curl http://localhost:19000/config_dump

curl http://localhost:19000/config_dump  | jq '.configs[1].dynamic_active_clusters' | grep null

sleep 20

curl http://localhost:19000/config_dump  | jq '.configs[1].dynamic_active_clusters'
