#!/bin/bash

# directory:coverage_percent
# for existing directories with low coverage.
declare -a KNOWN_LOW_COVERAGE=(
"source/common/network:95.6"
"source/common/http/http3:50.0"
"source/common/tracing:94.9"
"source/common/protobuf:94.3"
"source/common/secret:95.2"
"source/common/singleton:95.1"
"source/common/api:72.9"
"source/common/api/posix:71.8"
"source/common/init:96.2"
"source/common/json:90.6"
"source/common/thread:0.0" # Death tests don't report LCOV
"source/common/thread_local:95.7"
"source/common/crypto:0.0"
"source/common/common:96.1"
"source/common/common/posix:94.1"
"source/common/signal:90.4"
"source/common/watchdog:42.9" # Death tests don't report LCOV
"source/exe:93.7"
"source/extensions:96.3"
"source/extensions/common:94.4"
"source/extensions/common/crypto:91.5"
"source/extensions/common/tap:95.1"
"source/extensions/common/wasm:85.4"
"source/extensions/common/wasm/v8:85.4"
"source/extensions/common/wasm/null:77.8"
"source/extensions/filters/network/sni_cluster:90.3"
"source/extensions/filters/network/sni_dynamic_forward_proxy:90.9"
"source/extensions/filters/network/dubbo_proxy:96.1"
"source/extensions/filters/network/dubbo_proxy/router:95.1"
"source/extensions/filters/network/mongo_proxy:94.0"
"source/extensions/filters/network/common:96.1"
"source/extensions/filters/network/common/redis:96.2"
"source/extensions/filters/network/http_connection_manager:95.2"
"source/extensions/filters/http/cache:92.4"
"source/extensions/filters/http/cache/simple_http_cache:95.2"
"source/extensions/filters/http/dynamic_forward_proxy:94.9"
"source/extensions/filters/http/ip_tagging:91.2"
"source/extensions/filters/http/grpc_json_transcoder:93.3"
"source/extensions/filters/http/oauth2:96.5"
"source/extensions/filters/listener:96.0"
"source/extensions/filters/listener/tls_inspector:92.4"
"source/extensions/filters/listener/http_inspector:93.3"
"source/extensions/filters/udp:91.1"
"source/extensions/filters/udp/dns_filter:96.9"
"source/extensions/filters/common:94.7"
"source/extensions/filters/common/expr:92.2"
"source/extensions/filters/common/rbac:87.1"
"source/extensions/filters/common/fault:94.3"
"source/extensions/grpc_credentials:92.0"
"source/extensions/health_checkers:95.9"
"source/extensions/health_checkers/redis:95.9"
"source/extensions/quic_listeners:84.8"
"source/extensions/quic_listeners/quiche:84.8"
"source/extensions/stat_sinks/statsd:85.2"
"source/extensions/tracers:96.0"
"source/extensions/tracers/opencensus:91.2"
"source/extensions/tracers/xray:94.0"
"source/extensions/transport_sockets:95.3"
"source/extensions/transport_sockets/tap:95.6"
"source/extensions/transport_sockets/tls:94.2"
"source/extensions/transport_sockets/tls/ocsp:95.3"
"source/extensions/transport_sockets/tls/private_key:76.9"
"source/extensions/watchdog:69.6" # Death tests within extensions
"source/extensions/watchdog/profile_action:84.9"
"source/server:94.6"
"source/server/config_validation:76.6"
"source/server/admin:95.3"
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
