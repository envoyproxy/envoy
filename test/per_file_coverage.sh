#!/bin/bash

# directory:coverage_percent
# for existing directories with low coverage.
declare -a KNOWN_LOW_COVERAGE=(
"source/common:95.9" # Raise when QUIC coverage goes up
"source/common/api:76.5"
"source/common/api/posix:75.0"
"source/common/common/posix:92.7"
"source/common/config:96.5"
"source/common/crypto:0.0"
"source/common/event:94.1" # Emulated edge events guards don't report LCOV
"source/common/filesystem/posix:95.5"
"source/common/http:96.3"
"source/common/http/http2:96.4"
"source/common/json:89.8"
"source/common/matcher:94.0"
"source/common/network:94.4" # Flaky, `activateFileEvents`, `startSecureTransport` and `ioctl`, listener_socket do not always report LCOV
"source/common/network/dns_resolver:90.7"  # A few lines of MacOS code not tested in linux scripts. Tested in MacOS scripts
"source/common/protobuf:95.3"
"source/common/quic:91.8"
"source/common/router:96.5"
"source/common/secret:94.9"
"source/common/signal:86.9" # Death tests don't report LCOV
"source/common/singleton:95.7"
"source/common/tcp:94.6"
"source/common/thread:0.0" # Death tests don't report LCOV
"source/common/tracing:96.1"
"source/common/upstream:96.1"
"source/common/watchdog:58.6" # Death tests don't report LCOV
"source/exe:92.6"
"source/extensions/common:95.8"
"source/extensions/common/tap:94.2"
"source/extensions/common/wasm:95.2" # flaky: be careful adjusting
"source/extensions/common/wasm/ext:92.0"
"source/extensions/filters/common:96.1"
"source/extensions/filters/common/expr:96.2"
"source/extensions/filters/common/fault:94.5"
"source/extensions/filters/common/lua:96.5"
"source/extensions/filters/common/rbac:88.1"
"source/extensions/filters/http/cache:93.4"
"source/extensions/filters/http/cache/simple_http_cache:96.0"
"source/extensions/filters/http/grpc_json_transcoder:94.7"
"source/extensions/filters/http/ip_tagging:89.1"
"source/extensions/filters/http/kill_request:95.3" # Death tests don't report LCOV
"source/extensions/filters/http/lua:96.4"
"source/extensions/filters/http/wasm:95.8"
"source/extensions/filters/listener:95.9"
"source/extensions/filters/listener/http_inspector:95.8"
"source/extensions/filters/listener/original_dst:93.3"
"source/extensions/filters/listener/tls_inspector:92.3"
"source/extensions/filters/network/common:96.0"
"source/extensions/filters/network/common/redis:96.2"
"source/extensions/filters/network/mongo_proxy:95.5"
"source/extensions/filters/network/sni_cluster:88.9"
"source/extensions/filters/network/sni_dynamic_forward_proxy:95.2"
"source/extensions/filters/network/thrift_proxy/router:96.4"
"source/extensions/filters/network/wasm:95.7"
"source/extensions/filters/udp:96.4"
"source/extensions/filters/udp/dns_filter:96.1"
"source/extensions/health_checkers:95.7"
"source/extensions/health_checkers/redis:95.7"
"source/extensions/io_socket:96.2"
"source/extensions/io_socket/user_space:96.2"
"source/extensions/stat_sinks/common:96.4"
"source/extensions/stat_sinks/common/statsd:96.4"
"source/extensions/stat_sinks/graphite_statsd:88.5"
"source/extensions/stat_sinks/statsd:88.0"
"source/extensions/tracers/opencensus:94.8"
"source/extensions/tracers/xray:96.2"
"source/extensions/tracers/zipkin:96.1"
"source/extensions/transport_sockets:95.3"
"source/extensions/transport_sockets/tls:94.5"
"source/extensions/transport_sockets/tls/cert_validator:95.7"
"source/extensions/transport_sockets/tls/ocsp:96.5"
"source/extensions/transport_sockets/tls/private_key:77.8"
"source/extensions/wasm_runtime/wamr:0.0" # Not enabled in coverage build
"source/extensions/wasm_runtime/wasmtime:0.0" # Not enabled in coverage build
"source/extensions/wasm_runtime/wavm:0.0" # Not enabled in coverage build
"source/extensions/watchdog:83.3" # Death tests within extensions
"source/extensions/watchdog/profile_action:83.3"
"source/server:93.5" # flaky: be careful adjusting. See https://github.com/envoyproxy/envoy/issues/15239
"source/server/admin:95.3"
"source/server/config_validation:74.8"
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
