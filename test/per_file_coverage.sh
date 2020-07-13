#!/bin/bash

# directory:coverage_percent
# for existing extensions with low coverage.
declare -a KNOWN_LOW_COVERAGE=(
"source/extensions/common:94.0"
"source/extensions/common/crypto:91.5"
"source/extensions/common/wasm:85.4"
"source/extensions/common/wasm/null:77.8"
"source/extensions/common/wasm/v8:85.4"
"source/extensions/filters/common:94.6"
"source/extensions/filters/common/expr:92.2"
"source/extensions/filters/common/fault:95.8"
"source/extensions/filters/common/lua:95.9"
"source/extensions/filters/common/rbac:87.2"
"source/extensions/filters/http/aws_lambda:96.4"
"source/extensions/filters/http/aws_request_signing:93.3"
"source/extensions/filters/http/cache:80.7"
"source/extensions/filters/http/cache/simple_http_cache:84.5"
"source/extensions/filters/http/dynamic_forward_proxy:91.5"
"source/extensions/filters/http/grpc_json_transcoder:93.3"
"source/extensions/filters/http/ip_tagging:91.2"
"source/extensions/filters/listener:95.6"
"source/extensions/filters/listener/http_inspector:93.3"
"source/extensions/filters/listener/tls_inspector:92.4"
"source/extensions/filters/network/common:96.0"
"source/extensions/filters/network/common/redis:96.2"
"source/extensions/filters/network/direct_response:89.3"
"source/extensions/filters/network/dubbo_proxy:96.1"
"source/extensions/filters/network/dubbo_proxy/router:95.1"
"source/extensions/filters/network/mongo_proxy:94.0"
"source/extensions/filters/network/sni_cluster:90.3"
"source/extensions/filters/network/sni_dynamic_forward_proxy:89.4"
"source/extensions/filters/network/thrift_proxy/router:96.0"
"source/extensions/filters/udp:91.0"
"source/extensions/filters/udp/dns_filter:88.5"
"source/extensions/grpc_credentials:92.0"
"source/extensions/grpc_credentials/aws_iam:86.8"
"source/extensions/health_checkers:95.9"
"source/extensions/health_checkers/redis:95.9"
"source/extensions/quic_listeners:84.8"
"source/extensions/quic_listeners/quiche:84.8"
"source/extensions/retry:95.5"
"source/extensions/retry/host:85.7"
"source/extensions/retry/host/omit_canary_hosts:64.3"
"source/extensions/retry/host/previous_hosts:82.4"
"source/extensions/stat_sinks/statsd:85.2"
"source/extensions/tracers:96.3"
"source/extensions/tracers/opencensus:90.1"
"source/extensions/tracers/xray:95.3"
"source/extensions/transport_sockets:94.8"
"source/extensions/transport_sockets/tap:95.6"
"source/extensions/transport_sockets/tls:94.2"
"source/extensions/transport_sockets/tls/private_key:76.9"
)

[[ -z "${SRCDIR}" ]] && SRCDIR="${PWD}"
COVERAGE_DIR="${SRCDIR}"/generated/coverage
COVERAGE_DATA="${COVERAGE_DIR}/coverage.dat"

FAILED=0
DEFAULT_COVERAGE_THRESHOLD=96.6
DIRECTORY_THRESHOLD=$DEFAULT_COVERAGE_THRESHOLD

# Unfortunately we have a bunch of preexisting extensions with low coverage.
# Set their low bar as their current coverage level.
get_coverage_target() {
  DIRECTORY_THRESHOLD=$DEFAULT_COVERAGE_THRESHOLD
  for FILE_PERCENT in ${KNOWN_LOW_COVERAGE[@]}
  do
    if [[ $FILE_PERCENT =~ "$1:" ]]; then
      DIRECTORY_THRESHOLD=$(echo $FILE_PERCENT | sed 's/.*://')
      return
    fi
  done
}

# Make sure that for each extension directory with code, coverage doesn't dip
# below the default coverage threshold.
for DIRECTORY in $(find source/extensions/* -type d)
do
  get_coverage_target $DIRECTORY
  COVERAGE_VALUE=$(lcov -e $COVERAGE_DATA  "$DIRECTORY/*" -o /dev/null | grep line |  cut -d ' ' -f 4)
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
  if test ${COVERAGE_FAILED} -eq 1; then
    echo Code coverage for extension ${DIRECTORY} is lower than limit of ${DIRECTORY_THRESHOLD} \(${COVERAGE_VALUE}\)
    FAILED=1
  fi
done

exit $FAILED
