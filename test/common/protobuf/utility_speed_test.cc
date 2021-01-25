#include "common/protobuf/utility.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "benchmark/benchmark.h"
#include "test/test_common/utility.h"

namespace Envoy {

static void BM_ProtobufMessageHash(benchmark::State& state) {
  envoy::config::bootstrap::v3::Bootstrap bootstrap;
  TestUtility::loadFromYaml(R"EOF(
admin:
  access_log_path: /tmp/admin_access.log
  address:
    socket_address: { address: 127.0.0.1, port_value: 9901 }

static_resources:
  listeners:
  - name: listener_0
    address:
      socket_address: { address: 127.0.0.1, port_value: 10000 }
    filter_chains:
    - filters:
      - name: envoy.filters.network.http_connection_manager
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
          stat_prefix: ingress_http
          codec_type: AUTO
          route_config:
            name: local_route
            virtual_hosts:
            - name: local_service
              domains: ["*"]
              routes:
              - match: { prefix: "/" }
                route: { cluster: some_service }
          http_filters:
          - name: envoy.filters.http.router
  clusters:
  - name: some_service
    connect_timeout: 0.25s
    type: STATIC
    lb_policy: ROUND_ROBIN
    load_assignment:
      cluster_name: some_service
      endpoints:
      - lb_endpoints:
        - endpoint:
            address:
              socket_address:
                address: 127.0.0.1
                port_value: 1234
  )EOF",
                            bootstrap);
  for (auto _ : state) {
    benchmark::DoNotOptimize(MessageUtil::hash(bootstrap));
  }
}
BENCHMARK(BM_ProtobufMessageHash);

} // namespace Envoy