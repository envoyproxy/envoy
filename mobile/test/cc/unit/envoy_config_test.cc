#include <string>
#include <vector>

#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"

#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/cc/log_level.h"
#include "library/common/api/external.h"
#include "library/common/data/utility.h"

#if defined(__APPLE__)
#include "source/extensions/network/dns_resolver/apple/apple_dns_impl.h"
#endif

namespace Envoy {
namespace {

using namespace Platform;

using envoy::config::bootstrap::v3::Bootstrap;
using DfpClusterConfig = ::envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::Not;
using testing::SizeIs;

DfpClusterConfig getDfpClusterConfig(const Bootstrap& bootstrap) {
  DfpClusterConfig cluster_config;
  const auto& clusters = bootstrap.static_resources().clusters();
  for (const auto& cluster : clusters) {
    if (cluster.name() == "base") {
      MessageUtil::unpackTo(cluster.cluster_type().typed_config(), cluster_config);
    }
  }
  return cluster_config;
}

TEST(TestConfig, ConfigIsApplied) {
  EngineBuilder engine_builder;
  engine_builder
#ifdef ENVOY_ENABLE_QUIC
      .setHttp3ConnectionOptions("5RTO")
      .setHttp3ClientConnectionOptions("MPQC")
      .addQuicHint("www.abc.com", 443)
      .addQuicHint("www.def.com", 443)
      .addQuicCanonicalSuffix(".opq.com")
      .addQuicCanonicalSuffix(".xyz.com")
#endif
      .addConnectTimeoutSeconds(123)
      .addDnsRefreshSeconds(456)
      .addDnsMinRefreshSeconds(567)
      .addDnsFailureRefreshSeconds(789, 987)
      .addDnsQueryTimeoutSeconds(321)
      .addH2ConnectionKeepaliveIdleIntervalMilliseconds(222)
      .addH2ConnectionKeepaliveTimeoutSeconds(333)
      .setAppVersion("1.2.3")
      .setAppId("1234-1234-1234")
      .setRuntimeGuard("test_feature_false", true)
      .enableDnsCache(true, /* save_interval_seconds */ 101)
      .addDnsPreresolveHostnames({"lyft.com", "google.com"})
      .setForceAlwaysUsev6(true)
      .setDeviceOs("probably-ubuntu-on-CI");

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  const std::string config_str = bootstrap->ShortDebugString();

  std::vector<std::string> must_contain = {
      "connect_timeout { seconds: 123 }",
      "dns_refresh_rate { seconds: 456 }",
      "dns_min_refresh_rate { seconds: 567 }",
      "dns_query_timeout { seconds: 321 }",
      "dns_failure_refresh_rate { base_interval { seconds: 789 } max_interval { seconds: 987 } }",
      "connection_idle_interval { nanos: 222000000 }",
      "connection_keepalive { timeout { seconds: 333 }",
#ifdef ENVOY_MOBILE_STATS_REPORTING
      "asdf.fake.website",
      "stats_flush_interval { seconds: 654 }",
#endif
#ifdef ENVOY_ENABLE_QUIC
      "connection_options: \"5RTO\"",
      "client_connection_options: \"MPQC\"",
      "hostname: \"www.abc.com\"",
      "hostname: \"www.def.com\"",
      "canonical_suffixes: \".opq.com\"",
      "canonical_suffixes: \".xyz.com\"",
#endif
      "key: \"dns_persistent_cache\" save_interval { seconds: 101 }",
      "key: \"always_use_v6\" value { bool_value: true }",
      "key: \"test_feature_false\" value { bool_value: true }",
      "key: \"device_os\" value { string_value: \"probably-ubuntu-on-CI\" } }",
      "key: \"app_version\" value { string_value: \"1.2.3\" } }",
      "key: \"app_id\" value { string_value: \"1234-1234-1234\" } }",
      "validation_context { trusted_ca {",
  };

  for (const auto& string : must_contain) {
    EXPECT_THAT(config_str, HasSubstr(string)) << "'" << string << "' not found in " << config_str;
  }
}

TEST(TestConfig, MultiFlag) {
  EngineBuilder engine_builder;
  engine_builder.setRuntimeGuard("test_feature_false", true)
      .setRuntimeGuard("test_feature_true", false);

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  const std::string bootstrap_str = bootstrap->ShortDebugString();
  EXPECT_THAT(bootstrap_str, HasSubstr("\"test_feature_false\" value { bool_value: true }"));
  EXPECT_THAT(bootstrap_str, HasSubstr("\"test_feature_true\" value { bool_value: false }"));
}

TEST(TestConfig, ConfigIsValid) {
  EngineBuilder engine_builder;
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();

  // Test per-platform DNS fixes.
#if defined(__APPLE__)
  EXPECT_THAT(bootstrap->DebugString(), Not(HasSubstr("envoy.network.dns_resolver.getaddrinfo")));
  EXPECT_THAT(bootstrap->DebugString(), HasSubstr("envoy.network.dns_resolver.apple"));
#else
  EXPECT_THAT(bootstrap->DebugString(), HasSubstr("envoy.network.dns_resolver.getaddrinfo"));
  EXPECT_THAT(bootstrap->DebugString(), Not(HasSubstr("envoy.network.dns_resolver.apple")));
#endif
}

TEST(TestConfig, SetGzipDecompression) {
  EngineBuilder engine_builder;

  engine_builder.enableGzipDecompression(false);
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->DebugString(), Not(HasSubstr("envoy.filters.http.decompressor")));

  engine_builder.enableGzipDecompression(true);
  bootstrap.reset();
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->DebugString(), HasSubstr("envoy.filters.http.decompressor"));
}

TEST(TestConfig, SetBrotliDecompression) {
  EngineBuilder engine_builder;

  engine_builder.enableBrotliDecompression(false);
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->DebugString(), Not(HasSubstr("brotli.decompressor.v3.Brotli")));

  engine_builder.enableBrotliDecompression(true);
  bootstrap.reset();
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->DebugString(), HasSubstr("brotli.decompressor.v3.Brotli"));
}

TEST(TestConfig, SetSocketTag) {
  EngineBuilder engine_builder;

  engine_builder.enableSocketTagging(false);
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->DebugString(), Not(HasSubstr("http.socket_tag.SocketTag")));

  engine_builder.enableSocketTagging(true);
  bootstrap.reset();
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->DebugString(), HasSubstr("http.socket_tag.SocketTag"));
}

#ifdef ENVOY_ENABLE_QUIC
TEST(TestConfig, SetAltSvcCache) {
  EngineBuilder engine_builder;
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->DebugString(), HasSubstr("alternate_protocols_cache"));
}
#endif

TEST(TestConfig, StreamIdleTimeout) {
  EngineBuilder engine_builder;

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("stream_idle_timeout { seconds: 15 }"));

  engine_builder.setStreamIdleTimeoutSeconds(42);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("stream_idle_timeout { seconds: 42 }"));
}

TEST(TestConfig, PerTryIdleTimeout) {
  EngineBuilder engine_builder;

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("per_try_idle_timeout { seconds: 15 }"));

  engine_builder.setPerTryIdleTimeoutSeconds(42);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("per_try_idle_timeout { seconds: 42 }"));
}

TEST(TestConfig, EnableInterfaceBinding) {
  EngineBuilder engine_builder;

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), Not(HasSubstr("enable_interface_binding")));

  engine_builder.enableInterfaceBinding(true);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("enable_interface_binding: true"));
}

TEST(TestConfig, EnableDrainPostDnsRefresh) {
  EngineBuilder engine_builder;

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), Not(HasSubstr("enable_drain_post_dns_refresh")));

  engine_builder.enableDrainPostDnsRefresh(true);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("enable_drain_post_dns_refresh: true"));
}

TEST(TestConfig, EnforceTrustChainVerification) {
  EngineBuilder engine_builder;

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), Not(HasSubstr("trust_chain_verification")));

  engine_builder.enforceTrustChainVerification(false);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(),
              HasSubstr("trust_chain_verification: ACCEPT_UNTRUSTED"));
}

TEST(TestConfig, AddMaxConnectionsPerHost) {
  EngineBuilder engine_builder;

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("max_connections { value: 7 }"));

  engine_builder.addMaxConnectionsPerHost(16);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("max_connections { value: 16 }"));
}

TEST(TestConfig, AddDnsPreresolveHostnames) {
  EngineBuilder engine_builder;
  engine_builder.addDnsPreresolveHostnames({"google.com", "lyft.com"});
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();

  Protobuf::RepeatedPtrField<envoy::config::core::v3::SocketAddress>
      expected_dns_preresolve_hostnames;
  auto& host_addr1 = *expected_dns_preresolve_hostnames.Add();
  host_addr1.set_address("google.com");
  host_addr1.set_port_value(443);
  auto& host_addr2 = *expected_dns_preresolve_hostnames.Add();
  host_addr2.set_address("lyft.com");
  host_addr2.set_port_value(443);
  EXPECT_TRUE(TestUtility::repeatedPtrFieldEqual(
      getDfpClusterConfig(*bootstrap).dns_cache_config().preresolve_hostnames(),
      expected_dns_preresolve_hostnames));

  // Resetting the DNS preresolve hostnames with just "google.com" now.
  engine_builder.addDnsPreresolveHostnames({"google.com"});
  bootstrap = engine_builder.generateBootstrap();
  expected_dns_preresolve_hostnames.Clear();
  auto& host_addr3 = *expected_dns_preresolve_hostnames.Add();
  host_addr3.set_address("google.com");
  host_addr3.set_port_value(443);
  EXPECT_TRUE(TestUtility::repeatedPtrFieldEqual(
      getDfpClusterConfig(*bootstrap).dns_cache_config().preresolve_hostnames(),
      expected_dns_preresolve_hostnames));
}

TEST(TestConfig, DisableHttp3) {
  EngineBuilder engine_builder;

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
#ifdef ENVOY_ENABLE_QUIC
  EXPECT_THAT(bootstrap->ShortDebugString(),
              HasSubstr("envoy.extensions.filters.http.alternate_protocols_cache.v3.FilterConfig"));
#endif
#ifndef ENVOY_ENABLE_QUIC
  EXPECT_THAT(
      bootstrap->ShortDebugString(),
      Not(HasSubstr("envoy.extensions.filters.http.alternate_protocols_cache.v3.FilterConfig")));
#endif

#ifdef ENVOY_ENABLE_QUIC
  engine_builder.enableHttp3(false);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(
      bootstrap->ShortDebugString(),
      Not(HasSubstr("envoy.extensions.filters.http.alternate_protocols_cache.v3.FilterConfig")));
#endif
}

#ifdef ENVOY_MOBILE_XDS
TEST(TestConfig, XdsConfig) {
  EngineBuilder engine_builder;
  const std::string host = "fake-td.googleapis.com";
  const uint32_t port = 12345;
  const std::string authority = absl::StrCat(host, ":", port);

  XdsBuilder xds_builder(/*xds_server_address=*/host,
                         /*xds_server_port=*/port);
  engine_builder.setXds(std::move(xds_builder));
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();

  auto& ads_config = bootstrap->dynamic_resources().ads_config();
  EXPECT_EQ(ads_config.api_type(), envoy::config::core::v3::ApiConfigSource::GRPC);
  EXPECT_EQ(ads_config.grpc_services(0).envoy_grpc().cluster_name(), "base");
  EXPECT_EQ(ads_config.grpc_services(0).envoy_grpc().authority(), authority);

  Protobuf::RepeatedPtrField<envoy::config::core::v3::SocketAddress>
      expected_dns_preresolve_hostnames;
  auto& host_addr1 = *expected_dns_preresolve_hostnames.Add();
  host_addr1.set_address(host);
  host_addr1.set_port_value(port);
  EXPECT_TRUE(TestUtility::repeatedPtrFieldEqual(
      getDfpClusterConfig(*bootstrap).dns_cache_config().preresolve_hostnames(),
      expected_dns_preresolve_hostnames));

  // With initial gRPC metadata.
  xds_builder = XdsBuilder(/*xds_server_address=*/host, /*xds_server_port=*/port);
  xds_builder.addInitialStreamHeader(/*header=*/"x-goog-api-key", /*value=*/"A1B2C3")
      .addInitialStreamHeader(/*header=*/"x-android-package",
                              /*value=*/"com.google.envoymobile.io.myapp");
  engine_builder.setXds(std::move(xds_builder));
  bootstrap = engine_builder.generateBootstrap();
  auto& ads_config_with_metadata = bootstrap->dynamic_resources().ads_config();
  EXPECT_EQ(ads_config_with_metadata.api_type(), envoy::config::core::v3::ApiConfigSource::GRPC);
  EXPECT_EQ(ads_config_with_metadata.grpc_services(0).envoy_grpc().cluster_name(), "base");
  EXPECT_EQ(ads_config_with_metadata.grpc_services(0).envoy_grpc().authority(), authority);
  EXPECT_EQ(ads_config_with_metadata.grpc_services(0).initial_metadata(0).key(), "x-goog-api-key");
  EXPECT_EQ(ads_config_with_metadata.grpc_services(0).initial_metadata(0).value(), "A1B2C3");
  EXPECT_EQ(ads_config_with_metadata.grpc_services(0).initial_metadata(1).key(),
            "x-android-package");
  EXPECT_EQ(ads_config_with_metadata.grpc_services(0).initial_metadata(1).value(),
            "com.google.envoymobile.io.myapp");
}

TEST(TestConfig, CopyConstructor) {
  EngineBuilder engine_builder;
  engine_builder.setRuntimeGuard("test_feature_false", true).enableGzipDecompression(false);

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  std::string bootstrap_str = bootstrap->ShortDebugString();
  EXPECT_THAT(bootstrap_str, HasSubstr("\"test_feature_false\" value { bool_value: true }"));
  EXPECT_THAT(bootstrap_str, Not(HasSubstr("envoy.filters.http.decompressor")));

  EngineBuilder engine_builder_copy(engine_builder);
  engine_builder_copy.enableGzipDecompression(true);
  XdsBuilder xdsBuilder("FAKE_XDS_SERVER", 0);
  xdsBuilder.addClusterDiscoveryService();
  engine_builder_copy.setXds(xdsBuilder);
  bootstrap_str = engine_builder_copy.generateBootstrap()->ShortDebugString();
  EXPECT_THAT(bootstrap_str, HasSubstr("\"test_feature_false\" value { bool_value: true }"));
  EXPECT_THAT(bootstrap_str, HasSubstr("envoy.filters.http.decompressor"));
  EXPECT_THAT(bootstrap_str, HasSubstr("FAKE_XDS_SERVER"));

  EngineBuilder engine_builder_copy2(engine_builder_copy);
  bootstrap_str = engine_builder_copy2.generateBootstrap()->ShortDebugString();
  EXPECT_THAT(bootstrap_str, HasSubstr("\"test_feature_false\" value { bool_value: true }"));
  EXPECT_THAT(bootstrap_str, HasSubstr("envoy.filters.http.decompressor"));
  EXPECT_THAT(bootstrap_str, HasSubstr("FAKE_XDS_SERVER"));
}
#endif

TEST(TestConfig, EnablePlatformCertificatesValidation) {
  EngineBuilder engine_builder;
  engine_builder.enablePlatformCertificatesValidation(false);
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(),
              Not(HasSubstr("envoy_mobile.cert_validator.platform_bridge_cert_validator")));
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("trusted_ca"));

  engine_builder.enablePlatformCertificatesValidation(true);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(),
              HasSubstr("envoy_mobile.cert_validator.platform_bridge_cert_validator"));
  EXPECT_THAT(bootstrap->ShortDebugString(), Not(HasSubstr("trusted_ca")));
}

// Implementation of StringAccessor which tracks the number of times it was used.
class TestStringAccessor : public StringAccessor {
public:
  explicit TestStringAccessor(std::string data) : data_(data) {}
  ~TestStringAccessor() override = default;

  // StringAccessor
  const std::string& get() const override {
    ++count_;
    return data_;
  }

  int count() { return count_; }

private:
  std::string data_;
  mutable int count_ = 0;
};

TEST(TestConfig, AddNativeFilters) {
  EngineBuilder engine_builder;

  std::string filter_name1 = "envoy.filters.http.buffer1";
  std::string filter_name2 = "envoy.filters.http.buffer2";
  std::string filter_config =
      "{\"@type\":\"type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer\","
      "\"max_request_bytes\":5242880}";
  engine_builder.addNativeFilter(filter_name1, filter_config);
  engine_builder.addNativeFilter(filter_name2, filter_config);

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  const std::string hcm_config =
      bootstrap->static_resources().listeners(0).api_listener().DebugString();
  EXPECT_THAT(hcm_config, HasSubstr(filter_name1));
  EXPECT_THAT(hcm_config, HasSubstr(filter_name2));
  EXPECT_THAT(hcm_config,
              HasSubstr("type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer"));
  EXPECT_THAT(hcm_config, HasSubstr(std::to_string(5242880)));
}

TEST(TestConfig, AddPlatformFilter) {
  EngineBuilder engine_builder;

  std::string filter_name = "test_platform_filter";

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  std::string bootstrap_str = bootstrap->ShortDebugString();
  EXPECT_THAT(bootstrap_str, Not(HasSubstr("http.platform_bridge.PlatformBridge")));
  EXPECT_THAT(bootstrap_str, Not(HasSubstr("platform_filter_name: \"" + filter_name + "\"")));

  engine_builder.addPlatformFilter(filter_name);
  bootstrap = engine_builder.generateBootstrap();
  bootstrap_str = bootstrap->ShortDebugString();
  EXPECT_THAT(bootstrap_str, HasSubstr("http.platform_bridge.PlatformBridge"));
  EXPECT_THAT(bootstrap_str, HasSubstr("platform_filter_name: \"" + filter_name + "\""));
}

// TODO(RyanTheOptimist): This test seems to be flaky. #2641
TEST(TestConfig, DISABLED_StringAccessors) {
  std::string name("accessor_name");
  EngineBuilder engine_builder;
  std::string data_string = "envoy string";
  auto accessor = std::make_shared<TestStringAccessor>(data_string);
  engine_builder.addStringAccessor(name, accessor);
  EngineSharedPtr engine = engine_builder.build();
  auto c_accessor = static_cast<envoy_string_accessor*>(Envoy::Api::External::retrieveApi(name));
  ASSERT_TRUE(c_accessor != nullptr);
  EXPECT_EQ(0, accessor->count());
  envoy_data data = c_accessor->get_string(c_accessor->context);
  EXPECT_EQ(1, accessor->count());
  EXPECT_EQ(data_string, Data::Utility::copyToString(data));
  release_envoy_data(data);
}

TEST(TestConfig, SetNodeId) {
  EngineBuilder engine_builder;
  const std::string default_node_id = "envoy-mobile";
  EXPECT_EQ(engine_builder.generateBootstrap()->node().id(), default_node_id);

  const std::string test_node_id = "my_test_node";
  engine_builder.setNodeId(test_node_id);
  EXPECT_EQ(engine_builder.generateBootstrap()->node().id(), test_node_id);
}

TEST(TestConfig, SetNodeLocality) {
  EngineBuilder engine_builder;
  const std::string region = "us-west-1";
  const std::string zone = "some_zone";
  const std::string sub_zone = "some_sub_zone";
  engine_builder.setNodeLocality(region, zone, sub_zone);
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_EQ(bootstrap->node().locality().region(), region);
  EXPECT_EQ(bootstrap->node().locality().zone(), zone);
  EXPECT_EQ(bootstrap->node().locality().sub_zone(), sub_zone);
}

TEST(TestConfig, SetNodeMetadata) {
  ProtobufWkt::Struct node_metadata;
  (*node_metadata.mutable_fields())["string_field"].set_string_value("some_string");
  (*node_metadata.mutable_fields())["bool_field"].set_bool_value(true);
  (*node_metadata.mutable_fields())["number_field"].set_number_value(3.14);
  EngineBuilder engine_builder;
  engine_builder.setNodeMetadata(node_metadata);
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_EQ(bootstrap->node().metadata().fields().at("string_field").string_value(), "some_string");
  EXPECT_EQ(bootstrap->node().metadata().fields().at("bool_field").bool_value(), true);
  EXPECT_EQ(bootstrap->node().metadata().fields().at("number_field").number_value(), 3.14);
}

#ifdef ENVOY_MOBILE_XDS
TEST(TestConfig, AddCdsLayer) {
  XdsBuilder xds_builder(/*xds_server_address=*/"fake-xds-server", /*xds_server_port=*/12345);
  xds_builder.addClusterDiscoveryService();
  EngineBuilder engine_builder;
  engine_builder.setXds(std::move(xds_builder));

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_EQ(bootstrap->dynamic_resources().cds_resources_locator(), "");
  EXPECT_EQ(bootstrap->dynamic_resources().cds_config().initial_fetch_timeout().seconds(),
            /*default_timeout=*/5);

  xds_builder = XdsBuilder(/*xds_server_address=*/"fake-xds-server", /*xds_server_port=*/12345);
  const std::string cds_resources_locator =
      "xdstp://traffic-director-global.xds.googleapis.com/envoy.config.cluster.v3.Cluster";
  const int timeout_seconds = 300;
  xds_builder.addClusterDiscoveryService(cds_resources_locator, timeout_seconds);
  engine_builder.setXds(std::move(xds_builder));
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_EQ(bootstrap->dynamic_resources().cds_resources_locator(), cds_resources_locator);
  EXPECT_EQ(bootstrap->dynamic_resources().cds_config().initial_fetch_timeout().seconds(),
            timeout_seconds);
  EXPECT_EQ(bootstrap->dynamic_resources().cds_config().api_config_source().api_type(),
            envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC);
  EXPECT_EQ(bootstrap->dynamic_resources().cds_config().api_config_source().transport_api_version(),
            envoy::config::core::v3::ApiVersion::V3);
}
#endif

} // namespace
} // namespace Envoy
