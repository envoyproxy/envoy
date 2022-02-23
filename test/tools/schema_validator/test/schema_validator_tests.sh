#!/bin/bash

set -e

# Router_check_tool binary path
PATH_BIN="${TEST_SRCDIR}/envoy"/test/tools/schema_validator/schema_validator_tool

# Config json path
PATH_CONFIG="${TEST_SRCDIR}/envoy"/test/tools/schema_validator/test/config

# No errors
"${PATH_BIN}" "-c" "${PATH_CONFIG}/lds.yaml" "-t" "discovery_response"

# No errors with deprecation and WiP checking
"${PATH_BIN}" "-c" "${PATH_CONFIG}/lds.yaml" "-t" "discovery_response" "--fail-on-wip" \
  "--fail-on-deprecated"

# No errors without fail on deprecated
"${PATH_BIN}" "-c" "${PATH_CONFIG}/lds_deprecated.yaml" "-t" "discovery_response"

# Fail on deprecated
BAD_CONFIG_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/lds_deprecated.yaml" "-t" \
  "discovery_response" "--fail-on-deprecated" 2>&1) ||
  echo "${BAD_CONFIG_OUTPUT:-no-output}"
if [[ "${BAD_CONFIG_OUTPUT}" != *"Using deprecated option 'envoy.config.listener.v3.FilterChain.use_proxy_proto' from file listener_components.proto"* ]]; then
  exit 1
fi

# Unknown field
BAD_CONFIG_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/lds_unknown.yaml" "-t" \
  "discovery_response" 2>&1) ||
  echo "${BAD_CONFIG_OUTPUT:-no-output}"
if [[ "${BAD_CONFIG_OUTPUT}" != *"reason INVALID_ARGUMENT:foo: Cannot find field."* ]]; then
  exit 1
fi

# No errors without fail on WiP
"${PATH_BIN}" "-c" "${PATH_CONFIG}/lds_wip.yaml" "-t" "discovery_response"

# Fail on WiP
BAD_CONFIG_OUTPUT=$("${PATH_BIN}" "-c" "${PATH_CONFIG}/lds_wip.yaml" "-t" \
  "discovery_response" "--fail-on-wip" 2>&1) ||
  echo "${BAD_CONFIG_OUTPUT:-no-output}"
if [[ "${BAD_CONFIG_OUTPUT}" != *"field 'envoy.config.core.v3.Http3ProtocolOptions.allow_extended_connect' is marked as work-in-progress"* ]]; then
  exit 1
fi
