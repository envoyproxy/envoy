#include <algorithm>
#include <fstream>
#include <regex>
#include <unordered_map>

#include "envoy/admin/v3/clusters.pb.h"
#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/admin/v3/memory.pb.h"
#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/json/json_object.h"

#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/profiler/profiler.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"

#include "test/server/http/admin_instance.h"
#include "test/test_common/logging.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::AllOf;
using testing::Ge;
using testing::HasSubstr;
using testing::Invoke;
using testing::NiceMock;
using testing::Property;
using testing::Ref;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Server {

INSTANTIATE_TEST_SUITE_P(IpVersions, AdminInstanceTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(AdminInstanceTest, AdminCpuProfiler) {
  Buffer::OwnedImpl data;
  Http::ResponseHeaderMapImpl header_map;

  // Can only get code coverage of AdminImpl::handlerCpuProfiler stopProfiler with
  // a real profiler linked in (successful call to startProfiler).
#ifdef PROFILER_AVAILABLE
  EXPECT_EQ(Http::Code::OK, postCallback("/cpuprofiler?enable=y", header_map, data));
  EXPECT_TRUE(Profiler::Cpu::profilerEnabled());
#else
  EXPECT_EQ(Http::Code::InternalServerError,
            postCallback("/cpuprofiler?enable=y", header_map, data));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
#endif

  EXPECT_EQ(Http::Code::OK, postCallback("/cpuprofiler?enable=n", header_map, data));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

TEST_P(AdminInstanceTest, AdminHeapProfilerOnRepeatedRequest) {
  Buffer::OwnedImpl data;
  Http::ResponseHeaderMapImpl header_map;
  auto repeatResultCode = Http::Code::BadRequest;
#ifndef PROFILER_AVAILABLE
  repeatResultCode = Http::Code::NotImplemented;
#endif

  postCallback("/heapprofiler?enable=y", header_map, data);
  EXPECT_EQ(repeatResultCode, postCallback("/heapprofiler?enable=y", header_map, data));

  postCallback("/heapprofiler?enable=n", header_map, data);
  EXPECT_EQ(repeatResultCode, postCallback("/heapprofiler?enable=n", header_map, data));
}

TEST_P(AdminInstanceTest, AdminHeapProfiler) {
  Buffer::OwnedImpl data;
  Http::ResponseHeaderMapImpl header_map;

  // The below flow need to begin with the profiler not running
  Profiler::Heap::stopProfiler();

#ifdef PROFILER_AVAILABLE
  EXPECT_EQ(Http::Code::OK, postCallback("/heapprofiler?enable=y", header_map, data));
  EXPECT_TRUE(Profiler::Heap::isProfilerStarted());
  EXPECT_EQ(Http::Code::OK, postCallback("/heapprofiler?enable=n", header_map, data));
#else
  EXPECT_EQ(Http::Code::NotImplemented, postCallback("/heapprofiler?enable=y", header_map, data));
  EXPECT_FALSE(Profiler::Heap::isProfilerStarted());
  EXPECT_EQ(Http::Code::NotImplemented, postCallback("/heapprofiler?enable=n", header_map, data));
#endif

  EXPECT_FALSE(Profiler::Heap::isProfilerStarted());
}

TEST_P(AdminInstanceTest, MutatesErrorWithGet) {
  Buffer::OwnedImpl data;
  Http::ResponseHeaderMapImpl header_map;
  const std::string path("/healthcheck/fail");
  // TODO(jmarantz): the call to getCallback should be made to fail, but as an interim we will
  // just issue a warning, so that scripts using curl GET commands to mutate state can be fixed.
  EXPECT_LOG_CONTAINS("error",
                      "admin path \"" + path + "\" mutates state, method=GET rather than POST",
                      EXPECT_EQ(Http::Code::MethodNotAllowed, getCallback(path, header_map, data)));
}

TEST_P(AdminInstanceTest, AdminBadProfiler) {
  Buffer::OwnedImpl data;
  AdminImpl admin_bad_profile_path(TestEnvironment::temporaryPath("some/unlikely/bad/path.prof"),
                                   server_);
  Http::ResponseHeaderMapImpl header_map;
  const absl::string_view post = Http::Headers::get().MethodValues.Post;
  request_headers_.setMethod(post);
  admin_filter_.decodeHeaders(request_headers_, false);
  EXPECT_NO_LOGS(EXPECT_EQ(Http::Code::InternalServerError,
                           admin_bad_profile_path.runCallback("/cpuprofiler?enable=y", header_map,
                                                              data, admin_filter_)));
  EXPECT_FALSE(Profiler::Cpu::profilerEnabled());
}

TEST_P(AdminInstanceTest, WriteAddressToFile) {
  std::ifstream address_file(address_out_path_);
  std::string address_from_file;
  std::getline(address_file, address_from_file);
  EXPECT_EQ(admin_.socket().localAddress()->asString(), address_from_file);
}

TEST_P(AdminInstanceTest, AdminBadAddressOutPath) {
  std::string bad_path = TestEnvironment::temporaryPath("some/unlikely/bad/path/admin.address");
  AdminImpl admin_bad_address_out_path(cpu_profile_path_, server_);
  EXPECT_LOG_CONTAINS(
      "critical", "cannot open admin address output file " + bad_path + " for writing.",
      admin_bad_address_out_path.startHttpListener(
          "/dev/null", bad_path, Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr,
          listener_scope_.createScope("listener.admin.")));
  EXPECT_FALSE(std::ifstream(bad_path));
}

TEST_P(AdminInstanceTest, CustomHandler) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };

  // Test removable handler.
  EXPECT_NO_LOGS(EXPECT_TRUE(admin_.addHandler("/foo/bar", "hello", callback, true, false)));
  Http::ResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::Accepted, getCallback("/foo/bar", header_map, response));

  // Test that removable handler gets removed.
  EXPECT_TRUE(admin_.removeHandler("/foo/bar"));
  EXPECT_EQ(Http::Code::NotFound, getCallback("/foo/bar", header_map, response));
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));

  // Add non removable handler.
  EXPECT_TRUE(admin_.addHandler("/foo/bar", "hello", callback, false, false));
  EXPECT_EQ(Http::Code::Accepted, getCallback("/foo/bar", header_map, response));

  // Add again and make sure it is not there twice.
  EXPECT_FALSE(admin_.addHandler("/foo/bar", "hello", callback, false, false));

  // Try to remove non removable handler, and make sure it is not removed.
  EXPECT_FALSE(admin_.removeHandler("/foo/bar"));
  EXPECT_EQ(Http::Code::Accepted, getCallback("/foo/bar", header_map, response));
}

TEST_P(AdminInstanceTest, RejectHandlerWithXss) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };
  EXPECT_LOG_CONTAINS("error",
                      "filter \"/foo<script>alert('hi')</script>\" contains invalid character '<'",
                      EXPECT_FALSE(admin_.addHandler("/foo<script>alert('hi')</script>", "hello",
                                                     callback, true, false)));
}

TEST_P(AdminInstanceTest, RejectHandlerWithEmbeddedQuery) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };
  EXPECT_LOG_CONTAINS("error",
                      "filter \"/bar?queryShouldNotBeInPrefix\" contains invalid character '?'",
                      EXPECT_FALSE(admin_.addHandler("/bar?queryShouldNotBeInPrefix", "hello",
                                                     callback, true, false)));
}

TEST_P(AdminInstanceTest, EscapeHelpTextWithPunctuation) {
  auto callback = [](absl::string_view, Http::HeaderMap&, Buffer::Instance&,
                     AdminStream&) -> Http::Code { return Http::Code::Accepted; };

  // It's OK to have help text with HTML characters in it, but when we render the home
  // page they need to be escaped.
  const std::string planets = "jupiter>saturn>mars";
  EXPECT_TRUE(admin_.addHandler("/planets", planets, callback, true, false));

  Http::ResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/", header_map, response));
  const Http::HeaderString& content_type = header_map.ContentType()->value();
  EXPECT_THAT(std::string(content_type.getStringView()), testing::HasSubstr("text/html"));
  EXPECT_EQ(-1, response.search(planets.data(), planets.size(), 0));
  const std::string escaped_planets = "jupiter&gt;saturn&gt;mars";
  EXPECT_NE(-1, response.search(escaped_planets.data(), escaped_planets.size(), 0));
}

TEST_P(AdminInstanceTest, HelpUsesFormForMutations) {
  Http::ResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/", header_map, response));
  const std::string logging_action = "<form action='logging' method='post'";
  const std::string stats_href = "<a href='stats'";
  EXPECT_NE(-1, response.search(logging_action.data(), logging_action.size(), 0));
  EXPECT_NE(-1, response.search(stats_href.data(), stats_href.size(), 0));
}

TEST_P(AdminInstanceTest, ConfigDump) {
  Buffer::OwnedImpl response;
  Http::ResponseHeaderMapImpl header_map;
  auto entry = admin_.getConfigTracker().add("foo", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("bar");
    return msg;
  });
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "bar"
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump", header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

TEST_P(AdminInstanceTest, ConfigDumpMaintainsOrder) {
  // Add configs in random order and validate config_dump dumps in the order.
  auto bootstrap_entry = admin_.getConfigTracker().add("bootstrap", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("bootstrap_config");
    return msg;
  });
  auto route_entry = admin_.getConfigTracker().add("routes", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("routes_config");
    return msg;
  });
  auto listener_entry = admin_.getConfigTracker().add("listeners", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("listeners_config");
    return msg;
  });
  auto cluster_entry = admin_.getConfigTracker().add("clusters", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("clusters_config");
    return msg;
  });
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "bootstrap_config"
  },
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "clusters_config"
  },
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "listeners_config"
  },
  {
   "@type": "type.googleapis.com/google.protobuf.StringValue",
   "value": "routes_config"
  }
 ]
}
)EOF";
  // Run it multiple times and validate that order is preserved.
  for (size_t i = 0; i < 5; i++) {
    Buffer::OwnedImpl response;
    Http::ResponseHeaderMapImpl header_map;
    EXPECT_EQ(Http::Code::OK, getCallback("/config_dump", header_map, response));
    const std::string output = response.toString();
    EXPECT_EQ(expected_json, output);
  }
}

// Test that using the resource query parameter filters the config dump.
// We add both static and dynamic listener config to the dump, but expect only
// dynamic in the JSON with ?resource=dynamic_listeners.
TEST_P(AdminInstanceTest, ConfigDumpFiltersByResource) {
  Buffer::OwnedImpl response;
  Http::ResponseHeaderMapImpl header_map;
  auto listeners = admin_.getConfigTracker().add("listeners", [] {
    auto msg = std::make_unique<envoy::admin::v3::ListenersConfigDump>();
    auto dyn_listener = msg->add_dynamic_listeners();
    dyn_listener->set_name("foo");
    auto stat_listener = msg->add_static_listeners();
    envoy::config::listener::v3::Listener listener;
    listener.set_name("bar");
    stat_listener->mutable_listener()->PackFrom(listener);
    return msg;
  });
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.ListenersConfigDump.DynamicListener",
   "name": "foo"
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK,
            getCallback("/config_dump?resource=dynamic_listeners", header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

// Test that using the mask query parameter filters the config dump.
// We add both static and dynamic listener config to the dump, but expect only
// dynamic in the JSON with ?mask=dynamic_listeners.
TEST_P(AdminInstanceTest, ConfigDumpFiltersByMask) {
  Buffer::OwnedImpl response;
  Http::ResponseHeaderMapImpl header_map;
  auto listeners = admin_.getConfigTracker().add("listeners", [] {
    auto msg = std::make_unique<envoy::admin::v3::ListenersConfigDump>();
    auto dyn_listener = msg->add_dynamic_listeners();
    dyn_listener->set_name("foo");
    auto stat_listener = msg->add_static_listeners();
    envoy::config::listener::v3::Listener listener;
    listener.set_name("bar");
    stat_listener->mutable_listener()->PackFrom(listener);
    return msg;
  });
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.ListenersConfigDump",
   "dynamic_listeners": [
    {
     "name": "foo"
    }
   ]
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK,
            getCallback("/config_dump?mask=dynamic_listeners", header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

ProtobufTypes::MessagePtr testDumpClustersConfig() {
  auto msg = std::make_unique<envoy::admin::v3::ClustersConfigDump>();
  auto* static_cluster = msg->add_static_clusters();
  envoy::config::cluster::v3::Cluster inner_cluster;
  inner_cluster.set_name("foo");
  inner_cluster.set_ignore_health_on_host_removal(true);
  static_cluster->mutable_cluster()->PackFrom(inner_cluster);

  auto* dyn_cluster = msg->add_dynamic_active_clusters();
  dyn_cluster->set_version_info("baz");
  dyn_cluster->mutable_last_updated()->set_seconds(5);
  envoy::config::cluster::v3::Cluster inner_dyn_cluster;
  inner_dyn_cluster.set_name("bar");
  inner_dyn_cluster.set_ignore_health_on_host_removal(true);
  inner_dyn_cluster.mutable_http2_protocol_options()->set_allow_connect(true);
  dyn_cluster->mutable_cluster()->PackFrom(inner_dyn_cluster);
  return msg;
}

// Test that when using both resource and mask query parameters the JSON output contains
// only the desired resource and the fields specified in the mask.
TEST_P(AdminInstanceTest, ConfigDumpFiltersByResourceAndMask) {
  Buffer::OwnedImpl response;
  Http::ResponseHeaderMapImpl header_map;
  auto clusters = admin_.getConfigTracker().add("clusters", testDumpClustersConfig);
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.ClustersConfigDump.DynamicCluster",
   "version_info": "baz",
   "cluster": {
    "@type": "type.googleapis.com/envoy.config.cluster.v3.Cluster",
    "name": "bar",
    "http2_protocol_options": {
     "allow_connect": true
    }
   }
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK, getCallback("/config_dump?resource=dynamic_active_clusters&mask="
                                        "cluster.name,version_info,cluster.http2_protocol_options",
                                        header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

// Test that no fields are present in the JSON output if there is no intersection between the fields
// of the config dump and the fields present in the mask query parameter.
TEST_P(AdminInstanceTest, ConfigDumpNonExistentMask) {
  Buffer::OwnedImpl response;
  Http::ResponseHeaderMapImpl header_map;
  auto clusters = admin_.getConfigTracker().add("clusters", testDumpClustersConfig);
  const std::string expected_json = R"EOF({
 "configs": [
  {
   "@type": "type.googleapis.com/envoy.admin.v3.ClustersConfigDump.StaticCluster"
  }
 ]
}
)EOF";
  EXPECT_EQ(Http::Code::OK,
            getCallback("/config_dump?resource=static_clusters&mask=bad", header_map, response));
  std::string output = response.toString();
  EXPECT_EQ(expected_json, output);
}

// Test that a 404 Not found is returned if a non-existent resource is passed in as the
// resource query parameter.
TEST_P(AdminInstanceTest, ConfigDumpNonExistentResource) {
  Buffer::OwnedImpl response;
  Http::ResponseHeaderMapImpl header_map;
  auto listeners = admin_.getConfigTracker().add("listeners", [] {
    auto msg = std::make_unique<ProtobufWkt::StringValue>();
    msg->set_value("listeners_config");
    return msg;
  });
  EXPECT_EQ(Http::Code::NotFound, getCallback("/config_dump?resource=foo", header_map, response));
}

// Test that a 400 Bad Request is returned if the passed resource query parameter is not a
// repeated field.
TEST_P(AdminInstanceTest, ConfigDumpResourceNotRepeated) {
  Buffer::OwnedImpl response;
  Http::ResponseHeaderMapImpl header_map;
  auto clusters = admin_.getConfigTracker().add("clusters", [] {
    auto msg = std::make_unique<envoy::admin::v3::ClustersConfigDump>();
    msg->set_version_info("foo");
    return msg;
  });
  EXPECT_EQ(Http::Code::BadRequest,
            getCallback("/config_dump?resource=version_info", header_map, response));
}

TEST_P(AdminInstanceTest, Memory) {
  Http::ResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  EXPECT_EQ(Http::Code::OK, getCallback("/memory", header_map, response));
  const std::string output_json = response.toString();
  envoy::admin::v3::Memory output_proto;
  TestUtility::loadFromJson(output_json, output_proto);
  EXPECT_THAT(output_proto, AllOf(Property(&envoy::admin::v3::Memory::allocated, Ge(0)),
                                  Property(&envoy::admin::v3::Memory::heap_size, Ge(0)),
                                  Property(&envoy::admin::v3::Memory::pageheap_unmapped, Ge(0)),
                                  Property(&envoy::admin::v3::Memory::pageheap_free, Ge(0)),
                                  Property(&envoy::admin::v3::Memory::total_thread_cache, Ge(0))));
}

TEST_P(AdminInstanceTest, ContextThatReturnsNullCertDetails) {
  Http::ResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;

  // Setup a context that returns null cert details.
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context;
  envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext config;
  Extensions::TransportSockets::Tls::ClientContextConfigImpl cfg(config, factory_context);
  Stats::IsolatedStoreImpl store;
  Envoy::Ssl::ClientContextSharedPtr client_ctx(
      server_.sslContextManager().createSslClientContext(store, cfg));

  const std::string expected_empty_json = R"EOF({
 "certificates": [
  {
   "ca_cert": [],
   "cert_chain": []
  }
 ]
}
)EOF";

  // Validate that cert details are null and /certs handles it correctly.
  EXPECT_EQ(nullptr, client_ctx->getCaCertInformation());
  EXPECT_TRUE(client_ctx->getCertChainInformation().empty());
  EXPECT_EQ(Http::Code::OK, getCallback("/certs", header_map, response));
  EXPECT_EQ(expected_empty_json, response.toString());
}

TEST_P(AdminInstanceTest, ReopenLogs) {
  Http::ResponseHeaderMapImpl header_map;
  Buffer::OwnedImpl response;
  testing::NiceMock<AccessLog::MockAccessLogManager> access_log_manager_;

  EXPECT_CALL(server_, accessLogManager()).WillRepeatedly(ReturnRef(access_log_manager_));
  EXPECT_CALL(access_log_manager_, reopen());
  EXPECT_EQ(Http::Code::OK, postCallback("/reopen_logs", header_map, response));
}

TEST_P(AdminInstanceTest, ClustersJson) {
  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  ON_CALL(server_.cluster_manager_, clusters()).WillByDefault(ReturnPointee(&cluster_map));

  NiceMock<Upstream::MockClusterMockPrioritySet> cluster;
  cluster_map.emplace(cluster.info_->name_, cluster);

  NiceMock<Upstream::Outlier::MockDetector> outlier_detector;
  ON_CALL(Const(cluster), outlierDetector()).WillByDefault(Return(&outlier_detector));
  ON_CALL(outlier_detector,
          successRateEjectionThreshold(
              Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
      .WillByDefault(Return(6.0));
  ON_CALL(outlier_detector,
          successRateEjectionThreshold(
              Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin))
      .WillByDefault(Return(9.0));

  ON_CALL(*cluster.info_, addedViaApi()).WillByDefault(Return(true));

  Upstream::MockHostSet* host_set = cluster.priority_set_.getMockHostSet(0);
  auto host = std::make_shared<NiceMock<Upstream::MockHost>>();

  envoy::config::core::v3::Locality locality;
  locality.set_region("test_region");
  locality.set_zone("test_zone");
  locality.set_sub_zone("test_sub_zone");
  ON_CALL(*host, locality()).WillByDefault(ReturnRef(locality));

  host_set->hosts_.emplace_back(host);
  Network::Address::InstanceConstSharedPtr address =
      Network::Utility::resolveUrl("tcp://1.2.3.4:80");
  ON_CALL(*host, address()).WillByDefault(Return(address));
  const std::string hostname = "foo.com";
  ON_CALL(*host, hostname()).WillByDefault(ReturnRef(hostname));

  // Add stats in random order and validate that they come in order.
  Stats::PrimitiveCounter test_counter;
  test_counter.add(10);
  Stats::PrimitiveCounter rest_counter;
  rest_counter.add(10);
  Stats::PrimitiveCounter arest_counter;
  arest_counter.add(5);
  std::vector<std::pair<absl::string_view, Stats::PrimitiveCounterReference>> counters = {
      {"arest_counter", arest_counter},
      {"rest_counter", rest_counter},
      {"test_counter", test_counter},
  };
  Stats::PrimitiveGauge test_gauge;
  test_gauge.set(11);
  Stats::PrimitiveGauge atest_gauge;
  atest_gauge.set(10);
  std::vector<std::pair<absl::string_view, Stats::PrimitiveGaugeReference>> gauges = {
      {"atest_gauge", atest_gauge},
      {"test_gauge", test_gauge},
  };
  ON_CALL(*host, counters()).WillByDefault(Invoke([&counters]() { return counters; }));
  ON_CALL(*host, gauges()).WillByDefault(Invoke([&gauges]() { return gauges; }));

  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::FAILED_ACTIVE_HC))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::FAILED_OUTLIER_CHECK))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::FAILED_EDS_HEALTH))
      .WillByDefault(Return(false));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_ACTIVE_HC))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH))
      .WillByDefault(Return(true));
  ON_CALL(*host, healthFlagGet(Upstream::Host::HealthFlag::PENDING_DYNAMIC_REMOVAL))
      .WillByDefault(Return(true));

  ON_CALL(
      host->outlier_detector_,
      successRate(Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::ExternalOrigin))
      .WillByDefault(Return(43.2));
  ON_CALL(*host, weight()).WillByDefault(Return(5));
  ON_CALL(host->outlier_detector_,
          successRate(Upstream::Outlier::DetectorHostMonitor::SuccessRateMonitorType::LocalOrigin))
      .WillByDefault(Return(93.2));
  ON_CALL(*host, priority()).WillByDefault(Return(6));

  Buffer::OwnedImpl response;
  Http::ResponseHeaderMapImpl header_map;
  EXPECT_EQ(Http::Code::OK, getCallback("/clusters?format=json", header_map, response));
  std::string output_json = response.toString();
  envoy::admin::v3::Clusters output_proto;
  TestUtility::loadFromJson(output_json, output_proto);

  const std::string expected_json = R"EOF({
 "cluster_statuses": [
  {
   "name": "fake_cluster",
   "success_rate_ejection_threshold": {
    "value": 6
   },
   "local_origin_success_rate_ejection_threshold": {
    "value": 9
   },
   "added_via_api": true,
   "host_statuses": [
    {
     "address": {
      "socket_address": {
       "protocol": "TCP",
       "address": "1.2.3.4",
       "port_value": 80
      }
     },
     "stats": [
       {
       "name": "arest_counter",
       "value": "5",
       "type": "COUNTER"
       },
       {
       "name": "rest_counter",
       "value": "10",
       "type": "COUNTER"
      },
      {
       "name": "test_counter",
       "value": "10",
       "type": "COUNTER"
      },
      {
       "name": "atest_gauge",
       "value": "10",
       "type": "GAUGE"
      },
      {
       "name": "test_gauge",
       "value": "11",
       "type": "GAUGE"
      },
     ],
     "health_status": {
      "eds_health_status": "DEGRADED",
      "failed_active_health_check": true,
      "failed_outlier_check": true,
      "failed_active_degraded_check": true,
      "pending_dynamic_removal": true
     },
     "success_rate": {
      "value": 43.2
     },
     "weight": 5,
     "hostname": "foo.com",
     "priority": 6,
     "local_origin_success_rate": {
      "value": 93.2
     },
     "locality": {
       "region": "test_region",
       "zone": "test_zone",
       "sub_zone": "test_sub_zone"
     }
    }
   ]
  }
 ]
}
)EOF";

  envoy::admin::v3::Clusters expected_proto;
  TestUtility::loadFromJson(expected_json, expected_proto);

  // Ensure the protos created from each JSON are equivalent.
  EXPECT_THAT(output_proto, ProtoEq(expected_proto));

  // Ensure that the normal text format is used by default.
  Buffer::OwnedImpl response2;
  EXPECT_EQ(Http::Code::OK, getCallback("/clusters", header_map, response2));
  const std::string expected_text = R"EOF(fake_cluster::outlier::success_rate_average::0
fake_cluster::outlier::success_rate_ejection_threshold::6
fake_cluster::outlier::local_origin_success_rate_average::0
fake_cluster::outlier::local_origin_success_rate_ejection_threshold::9
fake_cluster::default_priority::max_connections::1
fake_cluster::default_priority::max_pending_requests::1024
fake_cluster::default_priority::max_requests::1024
fake_cluster::default_priority::max_retries::1
fake_cluster::high_priority::max_connections::1
fake_cluster::high_priority::max_pending_requests::1024
fake_cluster::high_priority::max_requests::1024
fake_cluster::high_priority::max_retries::1
fake_cluster::added_via_api::true
fake_cluster::1.2.3.4:80::arest_counter::5
fake_cluster::1.2.3.4:80::atest_gauge::10
fake_cluster::1.2.3.4:80::rest_counter::10
fake_cluster::1.2.3.4:80::test_counter::10
fake_cluster::1.2.3.4:80::test_gauge::11
fake_cluster::1.2.3.4:80::hostname::foo.com
fake_cluster::1.2.3.4:80::health_flags::/failed_active_hc/failed_outlier_check/degraded_active_hc/degraded_eds_health/pending_dynamic_removal
fake_cluster::1.2.3.4:80::weight::5
fake_cluster::1.2.3.4:80::region::test_region
fake_cluster::1.2.3.4:80::zone::test_zone
fake_cluster::1.2.3.4:80::sub_zone::test_sub_zone
fake_cluster::1.2.3.4:80::canary::false
fake_cluster::1.2.3.4:80::priority::6
fake_cluster::1.2.3.4:80::success_rate::43.2
fake_cluster::1.2.3.4:80::local_origin_success_rate::93.2
)EOF";
  EXPECT_EQ(expected_text, response2.toString());
}

TEST_P(AdminInstanceTest, GetRequest) {
  EXPECT_CALL(server_.options_, toCommandLineOptions()).WillRepeatedly(Invoke([] {
    Server::CommandLineOptionsPtr command_line_options =
        std::make_unique<envoy::admin::v3::CommandLineOptions>();
    command_line_options->set_restart_epoch(2);
    command_line_options->set_service_cluster("cluster");
    return command_line_options;
  }));
  NiceMock<Init::MockManager> initManager;
  ON_CALL(server_, initManager()).WillByDefault(ReturnRef(initManager));
  ON_CALL(server_.hot_restart_, version()).WillByDefault(Return("foo_version"));

  {
    Http::ResponseHeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initialized));
    EXPECT_EQ(Http::Code::OK, admin_.request("/server_info", "GET", response_headers, body));
    envoy::admin::v3::ServerInfo server_info_proto;
    EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
                HasSubstr("application/json"));

    // We only test that it parses as the proto and that some fields are correct, since
    // values such as timestamps + Envoy version are tricky to test for.
    TestUtility::loadFromJson(body, server_info_proto);
    EXPECT_EQ(server_info_proto.state(), envoy::admin::v3::ServerInfo::LIVE);
    EXPECT_EQ(server_info_proto.hot_restart_version(), "foo_version");
    EXPECT_EQ(server_info_proto.command_line_options().restart_epoch(), 2);
    EXPECT_EQ(server_info_proto.command_line_options().service_cluster(), "cluster");
  }

  {
    Http::ResponseHeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Uninitialized));
    EXPECT_EQ(Http::Code::OK, admin_.request("/server_info", "GET", response_headers, body));
    envoy::admin::v3::ServerInfo server_info_proto;
    EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
                HasSubstr("application/json"));

    // We only test that it parses as the proto and that some fields are correct, since
    // values such as timestamps + Envoy version are tricky to test for.
    TestUtility::loadFromJson(body, server_info_proto);
    EXPECT_EQ(server_info_proto.state(), envoy::admin::v3::ServerInfo::PRE_INITIALIZING);
    EXPECT_EQ(server_info_proto.command_line_options().restart_epoch(), 2);
    EXPECT_EQ(server_info_proto.command_line_options().service_cluster(), "cluster");
  }

  Http::ResponseHeaderMapImpl response_headers;
  std::string body;

  ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initializing));
  EXPECT_EQ(Http::Code::OK, admin_.request("/server_info", "GET", response_headers, body));
  envoy::admin::v3::ServerInfo server_info_proto;
  EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
              HasSubstr("application/json"));

  // We only test that it parses as the proto and that some fields are correct, since
  // values such as timestamps + Envoy version are tricky to test for.
  TestUtility::loadFromJson(body, server_info_proto);
  EXPECT_EQ(server_info_proto.state(), envoy::admin::v3::ServerInfo::INITIALIZING);
  EXPECT_EQ(server_info_proto.command_line_options().restart_epoch(), 2);
  EXPECT_EQ(server_info_proto.command_line_options().service_cluster(), "cluster");
}

TEST_P(AdminInstanceTest, GetReadyRequest) {
  NiceMock<Init::MockManager> initManager;
  ON_CALL(server_, initManager()).WillByDefault(ReturnRef(initManager));

  {
    Http::ResponseHeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initialized));
    EXPECT_EQ(Http::Code::OK, admin_.request("/ready", "GET", response_headers, body));
    EXPECT_EQ(body, "LIVE\n");
    EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
                HasSubstr("text/plain"));
  }

  {
    Http::ResponseHeaderMapImpl response_headers;
    std::string body;

    ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Uninitialized));
    EXPECT_EQ(Http::Code::ServiceUnavailable,
              admin_.request("/ready", "GET", response_headers, body));
    EXPECT_EQ(body, "PRE_INITIALIZING\n");
    EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
                HasSubstr("text/plain"));
  }

  Http::ResponseHeaderMapImpl response_headers;
  std::string body;

  ON_CALL(initManager, state()).WillByDefault(Return(Init::Manager::State::Initializing));
  EXPECT_EQ(Http::Code::ServiceUnavailable,
            admin_.request("/ready", "GET", response_headers, body));
  EXPECT_EQ(body, "INITIALIZING\n");
  EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
              HasSubstr("text/plain"));
}

TEST_P(AdminInstanceTest, PostRequest) {
  Http::ResponseHeaderMapImpl response_headers;
  std::string body;
  EXPECT_NO_LOGS(EXPECT_EQ(Http::Code::OK,
                           admin_.request("/healthcheck/fail", "POST", response_headers, body)));
  EXPECT_EQ(body, "OK\n");
  EXPECT_THAT(std::string(response_headers.ContentType()->value().getStringView()),
              HasSubstr("text/plain"));
}

} // namespace Server
} // namespace Envoy
