// End-to-end test of reporting the metrics of a gateway on behalf of the pods that it serves: each
// listener carries the metadata of the pod that it serves, a stats access logger tags its stats
// with that metadata, and the OTLP stats sink reports the stats under the resource of each pod.

#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/access_loggers/stats/v3/stats.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.h"

#include "test/integration/http_integration.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_map.h"
#include "gtest/gtest.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"

namespace Envoy {
namespace {

using ::opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using ::opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceResponse;

// Counts the requests of each listener, tagged with the pod that the listener's metadata names.
// The tags of a listener without metadata are empty.
constexpr absl::string_view StatsAccessLogConfig = R"EOF(
stats_scope:
  prefix: workload
counters:
- stat:
    name: requests
    tags:
    - name: k8s.pod.name
      value_format: '%METADATA(LISTENER:ambient:pod_name)%'
    - name: k8s.namespace.name
      value_format: '%METADATA(LISTENER:ambient:namespace_name)%'
    - name: http.response.status_code
      value_format: '%RESPONSE_CODE%'
  value_fixed: 1
)EOF";

// Converts the request counts to "workload.requests", reported under the k8s_pod resource that
// their tags name.
constexpr absl::string_view CustomMetricConversions = R"EOF(
matcher_tree:
  input:
    name: stat_full_name_match_input
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatFullNameMatchInput
  prefix_match_map:
    map:
      "workload.requests":
        action:
          name: convert
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.stat_sinks.open_telemetry.v3.SinkConfig.ConversionAction
            metric_name: workload.requests
            resource_conversion:
              static_resource_attributes:
              - key: gcp.resource_type
                value: {string_value: k8s_pod}
              tags_to_resource_attributes:
              - {tag_name: k8s.pod.name, resource_attribute_key: pod_name}
              - {tag_name: k8s.namespace.name, resource_attribute_key: namespace_name}
)EOF";

class ResourceConversionIntegrationTest
    : public testing::TestWithParam<Network::Address::IpVersion>,
      public HttpIntegrationTest {
public:
  ResourceConversionIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP1, GetParam()) {}

  void createUpstreams() override {
    HttpIntegrationTest::createUpstreams();
    // The OTLP collector.
    addFakeUpstream(Http::CodecType::HTTP2);
  }

  void initialize() override {
    config_helper_.addConfigModifier(
        [](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
               hcm) {
          auto* access_log = hcm.add_access_log();
          access_log->set_name("envoy.access_loggers.stats");
          envoy::extensions::access_loggers::stats::v3::Config stats_config;
          TestUtility::loadFromYaml(std::string(StatsAccessLogConfig), stats_config);
          std::ignore = access_log->mutable_typed_config()->PackFrom(stats_config);
        });
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* static_resources = bootstrap.mutable_static_resources();
      // Copies of the listener, which serve another pod and no pod.
      auto* pod_b_listener = static_resources->add_listeners();
      pod_b_listener->MergeFrom(static_resources->listeners(0));
      pod_b_listener->set_name("pod_b");
      auto* no_pod_listener = static_resources->add_listeners();
      no_pod_listener->MergeFrom(static_resources->listeners(0));
      no_pod_listener->set_name("no_pod");
      setPodMetadata(*static_resources->mutable_listeners(0), "pod-a", "ns-a");
      setPodMetadata(*pod_b_listener, "pod-b", "ns-b");

      auto* otlp_collector_cluster = static_resources->add_clusters();
      otlp_collector_cluster->MergeFrom(static_resources->clusters(0));
      otlp_collector_cluster->set_name("otlp_collector");
      ConfigHelper::setHttp2(*otlp_collector_cluster);

      envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
      sink_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("otlp_collector");
      sink_config.set_report_counters_as_deltas(true);
      TestUtility::loadFromYaml(std::string(CustomMetricConversions),
                                *sink_config.mutable_custom_metric_conversions());
      auto* metrics_sink = bootstrap.add_stats_sinks();
      metrics_sink->set_name("envoy.stat_sinks.open_telemetry");
      std::ignore = metrics_sink->mutable_typed_config()->PackFrom(sink_config);
      bootstrap.mutable_stats_flush_interval()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(100));
    });
    HttpIntegrationTest::initialize();
  }

  static void setPodMetadata(envoy::config::listener::v3::Listener& listener, absl::string_view pod,
                             absl::string_view ns) {
    auto& fields =
        *(*listener.mutable_metadata()->mutable_filter_metadata())["ambient"].mutable_fields();
    fields["pod_name"].set_string_value(std::string(pod));
    fields["namespace_name"].set_string_value(std::string(ns));
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ResourceConversionIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ResourceConversionIntegrationTest, ReportsMetricsUnderEachPodResource) {
  initialize();

  // Two requests through the listener of pod-a, and one through each of the other listeners.
  const Http::TestRequestHeaderMapImpl request_headers{
      {":method", "GET"}, {":path", "/"}, {":scheme", "http"}, {":authority", "host"}};
  for (const auto& [port_name, requests] :
       std::vector<std::pair<std::string, int>>{{"http", 2}, {"pod_b", 1}, {"no_pod", 1}}) {
    codec_client_ = makeHttpConnection(makeClientConnection(lookupPort(port_name)));
    for (int i = 0; i < requests; ++i) {
      sendRequestAndWaitForResponse(request_headers, 0, default_response_headers_, 0);
    }
    codec_client_->close();
  }

  FakeHttpConnectionPtr otlp_connection;
  ASSERT_TRUE(fake_upstreams_.back()->waitForHttpConnection(*dispatcher_, otlp_connection));
  // The streams are only destroyed after the connection is closed, since the thread of the fake
  // upstream may still use a stream after the test sends the response on it.
  std::vector<FakeStreamPtr> otlp_streams;
  absl::flat_hash_map<std::string, uint64_t> requests_by_pod;
  uint64_t requests_without_pod = 0;
  bool saw_proxy_metric = false;
  while (requests_by_pod["pod-a"] < 2 || requests_by_pod["pod-b"] < 1 || requests_without_pod < 1 ||
         !saw_proxy_metric) {
    FakeStreamPtr& stream = otlp_streams.emplace_back();
    ASSERT_TRUE(otlp_connection->waitForNewStream(*dispatcher_, stream));
    ExportMetricsServiceRequest request;
    ASSERT_TRUE(stream->waitForGrpcMessage(*dispatcher_, request));
    stream->startGrpcStream();
    stream->sendGrpcMessage(ExportMetricsServiceResponse());
    stream->finishGrpcStream(Grpc::Status::Ok);

    for (const auto& resource_metrics : request.resource_metrics()) {
      absl::flat_hash_map<std::string, std::string> resource;
      for (const auto& attr : resource_metrics.resource().attributes()) {
        resource[attr.key()] = attr.value().string_value();
      }
      for (const auto& scope_metrics : resource_metrics.scope_metrics()) {
        for (const auto& metric : scope_metrics.metrics()) {
          if (!resource.contains("pod_name")) {
            // The metrics of the proxy itself, and the request counts of the listener without a
            // pod, stay on the resource of the sink.
            EXPECT_NE(resource["gcp.resource_type"], "k8s_pod");
            if (metric.name() != "workload.requests") {
              saw_proxy_metric = true;
              continue;
            }
            for (const auto& data_point : metric.sum().data_points()) {
              absl::flat_hash_map<std::string, std::string> attributes;
              for (const auto& attr : data_point.attributes()) {
                attributes[attr.key()] = attr.value().string_value();
              }
              EXPECT_EQ(attributes, (absl::flat_hash_map<std::string, std::string>{
                                        {"k8s.pod.name", ""},
                                        {"k8s.namespace.name", ""},
                                        {"http.response.status_code", "200"}}));
              requests_without_pod += data_point.as_int();
            }
            continue;
          }
          EXPECT_EQ(metric.name(), "workload.requests");
          EXPECT_EQ(resource["gcp.resource_type"], "k8s_pod");
          const std::string& pod = resource["pod_name"];
          ASSERT_TRUE(pod == "pod-a" || pod == "pod-b") << pod;
          EXPECT_EQ(resource["namespace_name"], pod == "pod-a" ? "ns-a" : "ns-b");
          for (const auto& data_point : metric.sum().data_points()) {
            // The pod tags are on the resource, not on the data point.
            ASSERT_EQ(data_point.attributes_size(), 1);
            EXPECT_EQ(data_point.attributes(0).key(), "http.response.status_code");
            EXPECT_EQ(data_point.attributes(0).value().string_value(), "200");
            requests_by_pod[pod] += data_point.as_int();
          }
        }
      }
    }
  }
  EXPECT_EQ(requests_by_pod["pod-a"], 2);
  EXPECT_EQ(requests_by_pod["pod-b"], 1);
  EXPECT_EQ(requests_without_pod, 1);

  ASSERT_TRUE(otlp_connection->close());
  ASSERT_TRUE(otlp_connection->waitForDisconnect());
}

} // namespace
} // namespace Envoy
