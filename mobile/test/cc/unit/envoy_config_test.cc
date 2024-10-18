#include <sys/socket.h>

#include <string>
#include <vector>

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/socket_option.pb.h"
#include "envoy/extensions/clusters/dynamic_forward_proxy/v3/cluster.pb.h"
#include "envoy/extensions/filters/http/buffer/v3/buffer.pb.h"

#include "test/test_common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"
#include "library/cc/engine_builder.h"
#include "library/common/api/external.h"
#include "library/common/bridge//utility.h"

#if defined(__APPLE__)
#include "source/extensions/network/dns_resolver/apple/apple_dns_impl.h"
#endif

namespace Envoy {
namespace {

using namespace Platform;

using envoy::config::bootstrap::v3::Bootstrap;
using envoy::config::cluster::v3::Cluster;
using envoy::config::core::v3::SocketOption;
using DfpClusterConfig = ::envoy::extensions::clusters::dynamic_forward_proxy::v3::ClusterConfig;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::Not;
using testing::NotNull;
using testing::SizeIs;

DfpClusterConfig getDfpClusterConfig(const Bootstrap& bootstrap) {
  DfpClusterConfig cluster_config;
  const auto& clusters = bootstrap.static_resources().clusters();
  for (const auto& cluster : clusters) {
    if (cluster.name() == "base") {
      MessageUtil::unpackTo(cluster.cluster_type().typed_config(), cluster_config).IgnoreError();
    }
  }
  return cluster_config;
}

bool socketAddressesEqual(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::SocketAddress>& lhs,
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::SocketAddress>& rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }

  for (int i = 0; i < lhs.size(); ++i) {
    if ((lhs[i].address() != rhs[i].address()) || lhs[i].port_value() != rhs[i].port_value()) {
      return false;
    }
  }
  return true;
}

TEST(TestConfig, ConfigIsApplied) {
  EngineBuilder engine_builder;
  engine_builder.setHttp3ConnectionOptions("5RTO")
      .setHttp3ClientConnectionOptions("MPQC")
      .addQuicHint("www.abc.com", 443)
      .addQuicHint("www.def.com", 443)
      .addQuicCanonicalSuffix(".opq.com")
      .addQuicCanonicalSuffix(".xyz.com")
      .setNumTimeoutsToTriggerPortMigration(4)
      .addConnectTimeoutSeconds(123)
      .addDnsRefreshSeconds(456)
      .addDnsMinRefreshSeconds(567)
      .addDnsFailureRefreshSeconds(789, 987)
      .addDnsQueryTimeoutSeconds(321)
      .addH2ConnectionKeepaliveIdleIntervalMilliseconds(222)
      .addH2ConnectionKeepaliveTimeoutSeconds(333)
      .setAppVersion("1.2.3")
      .setAppId("1234-1234-1234")
      .addRuntimeGuard("test_feature_false", true)
      .enableDnsCache(true, /* save_interval_seconds */ 101)
      .addDnsPreresolveHostnames({"lyft.com", "google.com"})
      .setForceAlwaysUsev6(true)
      .setUseGroIfAvailable(true)
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
      "connection_options: \"5RTO\"",
      "client_connection_options: \"MPQC\"",
      "hostname: \"www.abc.com\"",
      "hostname: \"www.def.com\"",
      "canonical_suffixes: \".opq.com\"",
      "canonical_suffixes: \".xyz.com\"",
      "num_timeouts_to_trigger_port_migration { value: 4 }",
      "idle_network_timeout { seconds: 30 }",
      "key: \"dns_persistent_cache\" save_interval { seconds: 101 }",
      "key: \"always_use_v6\" value { bool_value: true }",
      "key: \"prefer_quic_client_udp_gro\" value { bool_value: true }",
      "key: \"test_feature_false\" value { bool_value: true }",
      "key: \"device_os\" value { string_value: \"probably-ubuntu-on-CI\" } }",
      "key: \"app_version\" value { string_value: \"1.2.3\" } }",
      "key: \"app_id\" value { string_value: \"1234-1234-1234\" } }",
      "validation_context { trusted_ca {",
      "initial_stream_window_size { value: 6291456 }",
      "initial_connection_window_size { value: 15728640 }"};

  for (const auto& string : must_contain) {
    EXPECT_THAT(config_str, HasSubstr(string)) << "'" << string << "' not found in " << config_str;
  }
}

TEST(TestConfig, MultiFlag) {
  EngineBuilder engine_builder;
  engine_builder.addRuntimeGuard("test_feature_false", true)
      .addRuntimeGuard("test_feature_true", false);

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

TEST(TestConfig, SetAltSvcCache) {
  EngineBuilder engine_builder;
  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->DebugString(), HasSubstr("alternate_protocols_cache"));
}

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

TEST(TestConfig, SetDnsQueryTimeout) {
  EngineBuilder engine_builder;

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  // The default value.
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("dns_query_timeout { seconds: 5 }"));

  engine_builder.addDnsQueryTimeoutSeconds(30);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(), HasSubstr("dns_query_timeout { seconds: 30 }"));
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
  EXPECT_TRUE(socketAddressesEqual(
      getDfpClusterConfig(*bootstrap).dns_cache_config().preresolve_hostnames(),
      expected_dns_preresolve_hostnames));

  // Resetting the DNS preresolve hostnames with just "google.com" now.
  engine_builder.addDnsPreresolveHostnames({"google.com"});
  bootstrap = engine_builder.generateBootstrap();
  expected_dns_preresolve_hostnames.Clear();
  auto& host_addr3 = *expected_dns_preresolve_hostnames.Add();
  host_addr3.set_address("google.com");
  host_addr3.set_port_value(443);
  EXPECT_TRUE(socketAddressesEqual(
      getDfpClusterConfig(*bootstrap).dns_cache_config().preresolve_hostnames(),
      expected_dns_preresolve_hostnames));
}

TEST(TestConfig, DisableHttp3) {
  EngineBuilder engine_builder;

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(bootstrap->ShortDebugString(),
              HasSubstr("envoy.extensions.filters.http.alternate_protocols_cache.v3.FilterConfig"));
  engine_builder.enableHttp3(false);
  bootstrap = engine_builder.generateBootstrap();
  EXPECT_THAT(
      bootstrap->ShortDebugString(),
      Not(HasSubstr("envoy.extensions.filters.http.alternate_protocols_cache.v3.FilterConfig")));
}

TEST(TestConfig, UdpSocketReceiveBufferSize) {
  EngineBuilder engine_builder;
  engine_builder.enableHttp3(true);

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  Cluster const* base_cluster = nullptr;
  for (const Cluster& cluster : bootstrap->static_resources().clusters()) {
    if (cluster.name() == "base") {
      base_cluster = &cluster;
      break;
    }
  }

  // The base H3 cluster should always be found.
  ASSERT_THAT(base_cluster, NotNull());

  SocketOption const* rcv_buf_option = nullptr;
  for (const SocketOption& sock_opt : base_cluster->upstream_bind_config().socket_options()) {
    if (sock_opt.name() == SO_RCVBUF) {
      rcv_buf_option = &sock_opt;
      break;
    }
  }

  // When using an H3 cluster, the UDP receive buffer size option should always be set.
  ASSERT_THAT(rcv_buf_option, NotNull());
  EXPECT_EQ(rcv_buf_option->level(), SOL_SOCKET);
  EXPECT_TRUE(rcv_buf_option->type().has_datagram());
  EXPECT_EQ(rcv_buf_option->int_value(), 1024 * 1024 /* 1 MB */);
}

TEST(TestConfig, UdpSocketSendBufferSize) {
  EngineBuilder engine_builder;
  engine_builder.enableHttp3(true);

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  Cluster const* base_cluster = nullptr;
  for (const Cluster& cluster : bootstrap->static_resources().clusters()) {
    if (cluster.name() == "base") {
      base_cluster = &cluster;
      break;
    }
  }

  // The base H3 cluster should always be found.
  ASSERT_THAT(base_cluster, NotNull());

  SocketOption const* snd_buf_option = nullptr;
  for (const SocketOption& sock_opt : base_cluster->upstream_bind_config().socket_options()) {
    if (sock_opt.name() == SO_SNDBUF) {
      snd_buf_option = &sock_opt;
      break;
    }
  }

  // When using an H3 cluster, the UDP send buffer size option should always be set.
  ASSERT_THAT(snd_buf_option, NotNull());
  EXPECT_EQ(snd_buf_option->level(), SOL_SOCKET);
  EXPECT_TRUE(snd_buf_option->type().has_datagram());
  EXPECT_EQ(snd_buf_option->int_value(), 1452 * 20);
}

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

  envoy::extensions::filters::http::buffer::v3::Buffer buffer;
  buffer.mutable_max_request_bytes()->set_value(5242880);
  ProtobufWkt::Any typed_config;
  typed_config.set_type_url("type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer");
  std::string serialized_buffer;
  buffer.SerializeToString(&serialized_buffer);
  typed_config.set_value(serialized_buffer);

  engine_builder.addNativeFilter(filter_name1, typed_config);
  engine_builder.addNativeFilter(filter_name2, typed_config);

  std::unique_ptr<Bootstrap> bootstrap = engine_builder.generateBootstrap();
  const std::string hcm_config =
      bootstrap->static_resources().listeners(0).api_listener().DebugString();
  EXPECT_THAT(hcm_config, HasSubstr(filter_name1));
  EXPECT_THAT(hcm_config, HasSubstr(filter_name2));
  EXPECT_THAT(hcm_config,
              HasSubstr("type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer"));
  EXPECT_THAT(hcm_config, HasSubstr(std::to_string(5242880)));
}

#ifdef ENVOY_ENABLE_FULL_PROTOS
TEST(TestConfig, AddTextProtoNativeFilters) {
  EngineBuilder engine_builder;

  std::string filter_name1 = "envoy.filters.http.buffer1";
  std::string filter_name2 = "envoy.filters.http.buffer2";
  std::string filter_config =
      "[type.googleapis.com/envoy.extensions.filters.http.buffer.v3.Buffer] { max_request_bytes { "
      "value: 5242880 } }";
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
#endif // ENVOY_ENABLE_FULL_PROTOS

// TODO(RyanTheOptimist): This test seems to be flaky. #2641
TEST(TestConfig, DISABLED_StringAccessors) {
  std::string name("accessor_name");
  EngineBuilder engine_builder;
  std::string data_string = "envoy string";
  auto accessor = std::make_shared<TestStringAccessor>(data_string);
  engine_builder.addStringAccessor(name, accessor);
  Platform::EngineSharedPtr engine = engine_builder.build();
  auto c_accessor = static_cast<envoy_string_accessor*>(Envoy::Api::External::retrieveApi(name));
  ASSERT_TRUE(c_accessor != nullptr);
  EXPECT_EQ(0, accessor->count());
  envoy_data data = c_accessor->get_string(c_accessor->context);
  EXPECT_EQ(1, accessor->count());
  EXPECT_EQ(data_string, Bridge::Utility::copyToString(data));
  release_envoy_data(data);
}

} // namespace
} // namespace Envoy
