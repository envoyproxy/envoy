# The filters to be fuzzed by the network_readfilter_fuzz_test. Prefer to add only stable filters,
# that do not have a dedicated fuzzer to them.
READFILTER_FUZZ_FILTERS = [
    "envoy.filters.network.client_ssl_auth",
    "envoy.filters.network.ext_authz",
    "envoy.filters.network.envoy_mobile_http_connection_manager",
    # A dedicated http_connection_manager fuzzer can be found in
    # test/common/http/conn_manager_impl_fuzz_test.cc
    "envoy.filters.network.http_connection_manager",
    "envoy.filters.network.local_ratelimit",
    "envoy.filters.network.ratelimit",
    "envoy.filters.network.rbac",
]

# These are marked as robust to downstream, but not currently fuzzed
READFILTER_NOFUZZ_FILTERS = [
    # TODO(asraa): Remove when fuzzer sets up connections for TcpProxy properly.
    "envoy.filters.network.tcp_proxy",
]
