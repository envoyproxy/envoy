#!/bin/bash

# directory:coverage_percent
# for existing directories with low coverage.
declare -a KNOWN_LOW_COVERAGE=(
"source/common:96.5" # Raise when QUIC coverage goes up
"source/common/api:75.3"
"source/common/api/posix:73.9"
"source/common/common:96.3"
"source/common/common/posix:94.1"
"source/common/crypto:0.0"
"source/common/event:94.2" # Emulated edge events guards don't report LCOV
"source/common/filesystem/posix:96.2"
"source/common/json:90.9"
"source/common/network:95.0"  # Flaky, `activateFileEvents`, `startSecureTransport` and `ioctl` do not always report LCOV
"source/common/protobuf:94.8"
"source/common/signal:84.5" # Death tests don't report LCOV
"source/common/singleton:95.8"
"source/common/thread:0.0" # Death tests don't report LCOV
"source/common/matcher:95.0"
"source/common/quic:90.6"
"source/common/tracing:96.1"
"source/common/watchdog:42.9" # Death tests don't report LCOV
"source/exe:94.3"
"source/extensions/common/crypto:91.5"
"source/extensions/common/tap:95.9"
"source/extensions/common/wasm:95.3" # flaky: be careful adjusting
"source/extensions/common/wasm/null:77.8"
"source/extensions/common/wasm/v8:85.4"
"source/extensions/filters/common/expr:96.4"
"source/extensions/filters/common/fault:94.6"
"source/extensions/filters/common/rbac:87.5"
"source/extensions/filters/http/cache:92.4"
"source/extensions/filters/http/cache/simple_http_cache:95.2"
"source/extensions/filters/http/grpc_json_transcoder:95.6"
"source/extensions/filters/http/ip_tagging:91.2"
"source/extensions/filters/http/kill_request:85.0" # Death tests don't report LCOV
"source/extensions/filters/listener/tls_inspector:92.4"
"source/extensions/filters/network/common:96.2"
"source/extensions/filters/network/common/redis:96.3"
"source/extensions/filters/network/dubbo_proxy:96.2"
"source/extensions/filters/network/mongo_proxy:94.0"
"source/extensions/filters/network/sni_cluster:90.3"
"source/extensions/filters/network/sni_dynamic_forward_proxy:90.9"
"source/extensions/health_checkers:95.9"
"source/extensions/health_checkers/redis:95.9"
"source/extensions/quic_listeners:85.1"
"source/extensions/stat_sinks/common/statsd:96.5"
"source/extensions/stat_sinks/graphite_statsd:85.7"
"source/extensions/stat_sinks/statsd:85.2"
"source/extensions/tracers/opencensus:92.5"
"source/extensions/tracers/xray:94.0"
"source/extensions/transport_sockets:95.7"
"source/extensions/transport_sockets/tls/cert_validator:96.5"
"source/extensions/transport_sockets/tls/private_key:76.9"
"source/extensions/transport_sockets/tls:95.1"
"source/extensions/wasm_runtime/wamr:0.0" # Not enabled in coverage build
"source/extensions/wasm_runtime/wasmtime:0.0" # Not enabled in coverage build
"source/extensions/wasm_runtime/wavm:0.0" # Not enabled in coverage build
"source/extensions/watchdog:85.7" # Death tests within extensions
"source/extensions/watchdog/profile_action:85.7"
"source/server:94.4" # flaky: be careful adjusting. See https://github.com/envoyproxy/envoy/issues/15239
"source/server/admin:95.8"
"source/server/config_validation:78.7"
)

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
COVERAGE_DIR="${SRCDIR}"/generated/coverage
COVERAGE_DATA="${COVERAGE_DIR}/coverage.dat"

FAILED=0
DEFAULT_COVERAGE_THRESHOLD=96.6
DIRECTORY_THRESHOLD=$DEFAULT_COVERAGE_THRESHOLD

# Unfortunately we have a bunch of preexisting directory with low coverage.
# Set their low bar as their current coverage level.
get_coverage_target() {
  DIRECTORY_THRESHOLD=$DEFAULT_COVERAGE_THRESHOLD
  for FILE_PERCENT in "${KNOWN_LOW_COVERAGE[@]}"
  do
    if [[ $FILE_PERCENT =~ $1: ]]; then
      DIRECTORY_THRESHOLD="${FILE_PERCENT//*:/}"
      return
    fi
  done
}

# Make sure that for each directory with code, coverage doesn't dip
# below the default coverage threshold.
SOURCES=$(find source/* -type d)
while read -r DIRECTORY
do
  get_coverage_target "$DIRECTORY"
  COVERAGE_VALUE=$(lcov -e "$COVERAGE_DATA"  "${DIRECTORY}/*" -o /dev/null | grep line |  cut -d ' ' -f 4)
  COVERAGE_VALUE=${COVERAGE_VALUE%?}
  # If the coverage number is 'n' (no data found) there is 0% coverage. This is
  # probably a directory without source code, so we skip checks.
  #
  # We could insist that we validate that 0% coverage directories are in a
  # documented list, but instead of adding busy-work for folks adding
  # non-source-containing directories, we trust reviewers to notice if there's
  # absolutely no tests for a full directory.
  if [[ $COVERAGE_VALUE =~ "n" ]]; then
    continue;
  fi;
  COVERAGE_FAILED=$(echo "${COVERAGE_VALUE}<${DIRECTORY_THRESHOLD}" | bc)
  if [[ "${COVERAGE_FAILED}" -eq 1 ]]; then
    echo "Code coverage for ${DIRECTORY} is lower than limit of ${DIRECTORY_THRESHOLD} (${COVERAGE_VALUE})"
    FAILED=1
  fi
done <<< "$SOURCES"

exit $FAILED
