#!/bin/bash -e

export NAME=rbac
export PORT_PROXY="${RBAC_PORT_PROXY:-10000}"
export PORT_ADMIN="${RBAC_PORT_ADMIN:-10001}"

# shellcheck source=examples/verify-common.sh
. "$(dirname "${BASH_SOURCE[0]}")/../verify-common.sh"

run_log "Test upstream with access denied response"
responds_with "RBAC: access denied" "http://localhost${PORT_PROXY}"

run_log "Test upstream without access denied response"
responds_without "RBAC: access denied" "http://localhost${PORT_PROXY}" -H "Referer: https://www.envoyproxy.io/docs/envoy"

run_log "Check admin stats"
responds_with rbac "http://localhost:${PORT_PROXY}/stats?fitler=rbac"