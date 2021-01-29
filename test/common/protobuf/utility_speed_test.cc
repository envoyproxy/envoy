#include "envoy/config/bootstrap/v3/bootstrap.pb.h"

#include "common/protobuf/utility.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "test/test_common/utility.h"

#include "benchmark/benchmark.h"

namespace Envoy {

static void bmProtobufMessageHashSmall(benchmark::State& state) {
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
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(MessageUtil::hash(bootstrap));
  }
}
BENCHMARK(bmProtobufMessageHashSmall);

static void bmProtobufMessageHashLarge(benchmark::State& state) {
  envoy::config::cluster::v3::Cluster cluster;
  cluster.set_name(std::string('a', 100));
  cluster.set_type(envoy::config::cluster::v3::Cluster_DiscoveryType_EDS);
  cluster.mutable_eds_cluster_config()->mutable_eds_config()->mutable_ads();
  for (int j = 0; j < 5; ++j) {
    cluster.mutable_circuit_breakers()->add_thresholds()->mutable_max_connections()->set_value(
        1000000 + j);
  }
  cluster.mutable_upstream_bind_config()->mutable_source_address()->set_resolver_name(
      "some.really.long.resolver.name");

  for (int j = 0; j < 10; ++j) {
    auto& filter_metadata = (*cluster.mutable_metadata()->mutable_filter_metadata())[absl::StrCat("KEY_NAME_", j)];
    for (int k = 0; k < 5; ++k) {
      (*filter_metadata.mutable_fields())[absl::StrCat("key_number_", k)].set_string_value(
          std::string('b', 30));
    }
  }

  for (auto _ : state) {
    UNREFERENCED_PARAMETER(_);
    benchmark::DoNotOptimize(MessageUtil::hash(cluster));
  }
}
BENCHMARK(bmProtobufMessageHashLarge);

} // namespace Envoy