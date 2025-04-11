#include <cstdint>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/load_balancing_policies/override_host/v3/override_host.pb.h"
#include "envoy/upstream/load_balancer.h"
#include "envoy/upstream/upstream.h"

#include "source/extensions/load_balancing_policies/override_host/config.h"
#include "source/extensions/load_balancing_policies/override_host/load_balancer.h"
#include "source/extensions/load_balancing_policies/override_host/metadata_keys.h"

#include "test/common/upstream/utility.h"
#include "test/extensions/load_balancing_policies/override_host/test_lb.pb.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/utility.h"

#include "absl/flags/flag.h"
#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolices {
namespace DynamicForwarding {
namespace {

using ::envoy::config::core::v3::Locality;
using ::envoy::extensions::load_balancing_policies::override_host::v3::DynamicForwarding;
using ::Envoy::Upstream::HostConstSharedPtr;
using ::Envoy::Upstream::HostMap;
using ::Envoy::Upstream::MockHostSet;
using ::test::load_balancing_policies::override_host::Config;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

class DynamicForwardingLoadBalancerTest : public ::testing::Test {
public:
  void SetUp() override {
    ON_CALL(load_balancer_context_, requestStreamInfo()).WillByDefault(Return(&stream_info_));
    ON_CALL(load_balancer_context_, downstreamHeaders())
        .WillByDefault(Return(&downstream_headers_));
  }

protected:
  void createLoadBalancer(const DynamicForwarding& config) {
    lb_config_ = factory_.loadConfig(server_factory_context_, config).value();
    thread_aware_lb_ =
        factory_.create(*lb_config_, *cluster_info_, main_thread_priority_set_,
                        server_factory_context_.runtime_loader_,
                        server_factory_context_.api_.random_, server_factory_context_.time_system_);
    ASSERT_TRUE(thread_aware_lb_->initialize().ok());
    thread_local_lb_factory_ = thread_aware_lb_->factory();
    load_balancer_ = thread_local_lb_factory_->create(lb_params_);
  }

  DynamicForwarding makeDefaultConfig() {
    DynamicForwarding config;
    Config locality_picker_config;
    auto* typed_extension_config =
        config.mutable_fallback_picking_policy()->add_policies()->mutable_typed_extension_config();
    typed_extension_config->mutable_typed_config()->PackFrom(locality_picker_config);
    typed_extension_config->set_name("envoy.load_balancing_policies.override_host.test");
    return config;
  }

  DynamicForwarding makeDefaultConfigWithHeadersEnabled(absl::string_view primary_header_name,
                                                        absl::string_view fallback_header_name) {
    DynamicForwarding config;
    Config locality_picker_config;
    auto* typed_extension_config =
        config.mutable_fallback_picking_policy()->add_policies()->mutable_typed_extension_config();
    typed_extension_config->mutable_typed_config()->PackFrom(locality_picker_config);
    typed_extension_config->set_name("envoy.load_balancing_policies.override_host.test");
    config.set_use_http_headers_for_endpoints(true);
    config.set_primary_endpoint_http_header_name(primary_header_name);
    config.set_fallback_endpoint_list_http_header_name(fallback_header_name);
    return config;
  }

  void setSelectedEndpointsMetadata(absl::string_view key,
                                    absl::string_view selected_endpoints_text_proto) {
    Envoy::ProtobufWkt::Struct selected_endpoints;
    EXPECT_TRUE(
        Protobuf::TextFormat::ParseFromString(selected_endpoints_text_proto, &selected_endpoints));
    (*metadata_.mutable_filter_metadata())[key] = selected_endpoints;
  }

  void makeCrossPriorityHostMap() {
    auto host_map = std::make_shared<HostMap>();
    for (const auto& host_set : thread_local_priority_set_.host_sets_) {
      for (const auto& host : host_set->hosts()) {
        host_map->insert({host->address()->asString(), host});
      }
    }
    thread_local_priority_set_.cross_priority_host_map_ = host_map;
  }

  Locality makeLocality(absl::string_view region, absl::string_view zone) {
    Locality locality;
    locality.set_region(region);
    locality.set_zone(zone);
    return locality;
  }

  void addHeader(absl::string_view header_name, absl::string_view header_value) {
    downstream_headers_.addCopy(std::string(header_name), std::string(header_value));
  }

  ::envoy::config::core::v3::Metadata metadata_;
  Envoy::Http::TestRequestHeaderMapImpl downstream_headers_;
  NiceMock<Envoy::Server::Configuration::MockServerFactoryContext> server_factory_context_;
  std::shared_ptr<Envoy::Upstream::ClusterInfo> cluster_info_ =
      std::make_shared<NiceMock<Envoy::Upstream::MockClusterInfo>>();
  NiceMock<Envoy::Upstream::MockPrioritySet> main_thread_priority_set_;
  NiceMock<Envoy::Upstream::MockPrioritySet> thread_local_priority_set_;
  NiceMock<Envoy::Upstream::MockLoadBalancerContext> load_balancer_context_;
  NiceMock<Envoy::StreamInfo::MockStreamInfo> stream_info_;
  DynamicForwardingLoadBalancerFactory factory_;
  Envoy::Upstream::LoadBalancerConfigPtr lb_config_;
  Envoy::Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Envoy::Upstream::LoadBalancerFactorySharedPtr thread_local_lb_factory_;
  Envoy::Upstream::LoadBalancerParams lb_params_{thread_local_priority_set_, nullptr};
  LoadBalancerPtr load_balancer_;
};

TEST_F(DynamicForwardingLoadBalancerTest, NoMetadatOrHeaders) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {Envoy::Upstream::makeTestHost(
      cluster_info_, "tcp://127.0.0.1:80", server_factory_context_.time_system_, us_central1_a, 1,
      0, Host::HealthStatus::HEALTHY)};
  host_set->hosts_per_locality_ = ::Envoy::Upstream::makeHostsPerLocality({{host_set->hosts_[0]}});
  makeCrossPriorityHostMap();

  createLoadBalancer(makeDefaultConfig());

  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  // Fallback LB is used.
  HostConstSharedPtr host = load_balancer_->chooseHost(&load_balancer_context_).host;
  EXPECT_EQ(host->address()->asString(), "127.0.0.1:80");

  EXPECT_FALSE(load_balancer_->lifetimeCallbacks().has_value());
  std::vector<uint8_t> out_value;
  EXPECT_FALSE(load_balancer_->selectExistingConnection(&load_balancer_context_, *host, out_value)
                   .has_value());
  EXPECT_EQ(load_balancer_->peekAnotherHost(&load_balancer_context_), nullptr);
}

TEST_F(DynamicForwardingLoadBalancerTest, NullptrHeaders) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {Envoy::Upstream::makeTestHost(
      cluster_info_, "tcp://127.0.0.1:80", server_factory_context_.time_system_, us_central1_a, 1,
      0, Host::HealthStatus::HEALTHY)};
  host_set->hosts_per_locality_ = ::Envoy::Upstream::makeHostsPerLocality({{host_set->hosts_[0]}});
  makeCrossPriorityHostMap();

  createLoadBalancer(makeDefaultConfig());

  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  // Ensure that `downstreamHeaders()` returning nullptr is handled correctly.
  EXPECT_CALL(load_balancer_context_, downstreamHeaders()).WillRepeatedly(Return(nullptr));

  // Fallback LB is used.
  HostConstSharedPtr host = load_balancer_->chooseHost(&load_balancer_context_).host;
  EXPECT_EQ(host->address()->asString(), "127.0.0.1:80");
}

TEST_F(DynamicForwardingLoadBalancerTest, NullptrCrossPriorityHostMap) {
  thread_local_priority_set_.cross_priority_host_map_ = nullptr;
  createLoadBalancer(makeDefaultConfig());

  setSelectedEndpointsMetadata("envoy.lb", R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value: { string_value: "[::1]:80" }
    }
  )pb");
  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  // No host should be selected as hosts sets are nullptr.
  EXPECT_EQ(load_balancer_->chooseHost(&load_balancer_context_).host, nullptr);
}

TEST_F(DynamicForwardingLoadBalancerTest, PrimaryAddressDoesNotExist) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");
  Locality us_central1_b = makeLocality("us-central1", "us-central1-b");
  Locality us_west3_c = makeLocality("us-west3", "us-west3-c");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[fda3:e722:ac3:cc00:172:b9fb:a00:2]:80",
                                    server_factory_context_.time_system_, us_central1_a, 1, 0,
                                    Host::HealthStatus::HEALTHY),
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[2002:a17:93c:a62::1]:80",
                                    server_factory_context_.time_system_, us_central1_b, 1, 0,
                                    Host::HealthStatus::UNHEALTHY),
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[fda3:e722:ac3:cc00:172:b9fb:a00:4]:80",
                                    server_factory_context_.time_system_, us_west3_c, 1, 0,
                                    Host::HealthStatus::DEGRADED)};
  host_set->hosts_per_locality_ = ::Envoy::Upstream::makeHostsPerLocality(
      {{host_set->hosts_[0]}, {host_set->hosts_[1]}, {host_set->hosts_[2]}});
  makeCrossPriorityHostMap();

  createLoadBalancer(makeDefaultConfig());

  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  load_balancer_->chooseHost(&load_balancer_context_);

  // Use non existent fallback address 1.2.3.4.
  setSelectedEndpointsMetadata("envoy.lb", R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value: { string_value: "1.2.3.4:80" }
    }
  )pb");
  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  HostConstSharedPtr host = load_balancer_->chooseHost(&load_balancer_context_).host;
  // Non existent primary address causes fallback LB to use use, which return the first host in the
  // set
  EXPECT_EQ(host->address()->asString(), "[fda3:e722:ac3:cc00:172:b9fb:a00:2]:80");
}

TEST_F(DynamicForwardingLoadBalancerTest, HeaderIsPreferredOverMetadata) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");
  Locality us_central1_b = makeLocality("us-central1", "us-central1-b");
  Locality us_west3_c = makeLocality("us-west3", "us-west3-c");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[2600:2d00:1:cc00:172:b9fb:a00:2]:80",
                                    server_factory_context_.time_system_, us_central1_a, 1, 0,
                                    Host::HealthStatus::HEALTHY),
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[2600:2d00:1:cc00:172:b9fb:a00:3]:80",
                                    server_factory_context_.time_system_, us_central1_b, 1, 0,
                                    Host::HealthStatus::UNHEALTHY),
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[2600:2d00:1:cc00:172:b9fb:a00:4]:80",
                                    server_factory_context_.time_system_, us_west3_c, 1, 0,
                                    Host::HealthStatus::DEGRADED)};
  host_set->hosts_per_locality_ = ::Envoy::Upstream::makeHostsPerLocality(
      {{host_set->hosts_[0]}, {host_set->hosts_[1]}, {host_set->hosts_[2]}});
  makeCrossPriorityHostMap();

  // Use the default header names.
  createLoadBalancer(makeDefaultConfigWithHeadersEnabled("", ""));

  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  load_balancer_->chooseHost(&load_balancer_context_);

  setSelectedEndpointsMetadata("envoy.lb", R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value: { string_value: "[2600:2d00:1:cc00:172:b9fb:a00:4]:80" }
    }
  )pb");
  addHeader(kPrimaryEndpointHeaderName, "[2600:2d00:1:cc00:172:b9fb:a00:3]:80");
  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  HostConstSharedPtr host = load_balancer_->chooseHost(&load_balancer_context_).host;
  // Expect the the address from the header to be used.
  EXPECT_EQ(host->address()->asString(), "[2600:2d00:1:cc00:172:b9fb:a00:3]:80");
  // Since there are no fallback hosts header, subsequent calls to chooseHost
  // will use the fallback LB policy.
  host = load_balancer_->chooseHost(&load_balancer_context_).host;
  EXPECT_EQ(host->address()->asString(), "[2600:2d00:1:cc00:172:b9fb:a00:2]:80");
}

TEST_F(DynamicForwardingLoadBalancerTest, UparseableHeaderValueUsesFallback) {
  // Validate that metadata is ignored if the header is present but its
  // value is not a valid IP address.
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");
  Locality us_central1_b = makeLocality("us-central1", "us-central1-b");
  Locality us_west3_c = makeLocality("us-west3", "us-west3-c");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[2600:2d00:1:cc00:172:b9fb:a00:2]:80",
                                    server_factory_context_.time_system_, us_central1_a, 1, 0,
                                    Host::HealthStatus::HEALTHY),
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[2600:2d00:1:cc00:172:b9fb:a00:3]:80",
                                    server_factory_context_.time_system_, us_central1_b, 1, 0,
                                    Host::HealthStatus::UNHEALTHY),
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[2600:2d00:1:cc00:172:b9fb:a00:4]:80",
                                    server_factory_context_.time_system_, us_west3_c, 1, 0,
                                    Host::HealthStatus::DEGRADED)};
  host_set->hosts_per_locality_ = ::Envoy::Upstream::makeHostsPerLocality(
      {{host_set->hosts_[0]}, {host_set->hosts_[1]}, {host_set->hosts_[2]}});
  makeCrossPriorityHostMap();

  // Use the default header names.
  createLoadBalancer(makeDefaultConfigWithHeadersEnabled("", ""));

  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  load_balancer_->chooseHost(&load_balancer_context_);

  setSelectedEndpointsMetadata("envoy.lb", R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value: { string_value: "[2600:2d00:1:cc00:172:b9fb:a00:4]:80" }
    }
  )pb");

  addHeader(kPrimaryEndpointHeaderName, "fff-bar-.bats@just.Wrong");
  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  // Fallback LB is used if the host value is invalid (not it return the first host in the set).
  HostConstSharedPtr host = load_balancer_->chooseHost(&load_balancer_context_).host;
  EXPECT_EQ(host->address()->asString(), "[2600:2d00:1:cc00:172:b9fb:a00:2]:80");
  // Calling choseHosts again results in the same behavior.
  host = load_balancer_->chooseHost(&load_balancer_context_).host;
  EXPECT_EQ(host->address()->asString(), "[2600:2d00:1:cc00:172:b9fb:a00:2]:80");
}

TEST_F(DynamicForwardingLoadBalancerTest, SelectIpv4EndpointWithHeader) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {Envoy::Upstream::makeTestHost(
                          cluster_info_, "tcp://1.2.3.4:80", server_factory_context_.time_system_,
                          us_central1_a, 1, 0, Host::HealthStatus::HEALTHY),
                      Envoy::Upstream::makeTestHost(
                          cluster_info_, "tcp://5.6.7.8:80", server_factory_context_.time_system_,
                          us_central1_a, 1, 0, Host::HealthStatus::HEALTHY)};
  host_set->hosts_per_locality_ =
      ::Envoy::Upstream::makeHostsPerLocality({{host_set->hosts_[0], host_set->hosts_[1]}});
  makeCrossPriorityHostMap();

  // Use custom header names.
  createLoadBalancer(
      makeDefaultConfigWithHeadersEnabled("x-foo-primary-endpoint", "x-foo-failover-endpoints"));

  addHeader("x-foo-primary-endpoint", "5.6.7.8:80");
  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  HostConstSharedPtr host = load_balancer_->chooseHost(&load_balancer_context_).host;
  EXPECT_EQ(host->address()->asString(), "5.6.7.8:80");
}

TEST_F(DynamicForwardingLoadBalancerTest, SelectIpv6EndpointWithHeader) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {Envoy::Upstream::makeTestHost(
                          cluster_info_, "tcp://[::1]:80", server_factory_context_.time_system_,
                          us_central1_a, 1, 0, Host::HealthStatus::HEALTHY),
                      Envoy::Upstream::makeTestHost(
                          cluster_info_, "tcp://[::2]:80", server_factory_context_.time_system_,
                          us_central1_a, 1, 0, Host::HealthStatus::HEALTHY)};
  host_set->hosts_per_locality_ =
      ::Envoy::Upstream::makeHostsPerLocality({{host_set->hosts_[0], host_set->hosts_[1]}});
  makeCrossPriorityHostMap();

  // Use custom header names.
  createLoadBalancer(
      makeDefaultConfigWithHeadersEnabled("x-foo-primary-endpoint", "x-foo-failover-endpoints"));

  addHeader("x-foo-primary-endpoint", "[::2]:80");
  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  HostConstSharedPtr host = load_balancer_->chooseHost(&load_balancer_context_).host;
  EXPECT_EQ(host->address()->asString(), "[::2]:80");
}

TEST_F(DynamicForwardingLoadBalancerTest, WrongHeaderName) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {Envoy::Upstream::makeTestHost(
                          cluster_info_, "tcp://[::1]:80", server_factory_context_.time_system_,
                          us_central1_a, 1, 0, Host::HealthStatus::HEALTHY),
                      Envoy::Upstream::makeTestHost(
                          cluster_info_, "tcp://[::2]:80", server_factory_context_.time_system_,
                          us_central1_a, 1, 0, Host::HealthStatus::HEALTHY)};
  host_set->hosts_per_locality_ =
      ::Envoy::Upstream::makeHostsPerLocality({{host_set->hosts_[0], host_set->hosts_[1]}});
  makeCrossPriorityHostMap();

  // Use custom header names.
  createLoadBalancer(
      makeDefaultConfigWithHeadersEnabled("x-foo-primary-endpoint", "x-foo-failover-endpoints"));

  setSelectedEndpointsMetadata("envoy.lb", R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value: { string_value: "[::2]:80" }
    }
  )pb");
  addHeader("x-foo-wrong-name", "[::1]:80");
  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  HostConstSharedPtr host = load_balancer_->chooseHost(&load_balancer_context_).host;
  // If the header name is wrong, the metadata value should be used.
  EXPECT_EQ(host->address()->asString(), "[::2]:80");
}

TEST_F(DynamicForwardingLoadBalancerTest, NullptrFromFallbackLb) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");

  thread_local_priority_set_.getMockHostSet(0);
  // Do not populate any hosts, so that the fallback LB returns nullptr.
  createLoadBalancer(makeDefaultConfig());

  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  // Without metadata or headers the fallback LB is used. Make sure there are
  // no crashes if it return nullptr host.
  EXPECT_EQ(load_balancer_->chooseHost(&load_balancer_context_).host, nullptr);
}

TEST_F(DynamicForwardingLoadBalancerTest, SelectIpv4EndpointUsingMetadata) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {Envoy::Upstream::makeTestHost(
      cluster_info_, "tcp://127.0.0.1:80", server_factory_context_.time_system_, us_central1_a, 1,
      0, Host::HealthStatus::HEALTHY)};
  host_set->hosts_per_locality_ = ::Envoy::Upstream::makeHostsPerLocality({{host_set->hosts_[0]}});
  makeCrossPriorityHostMap();

  createLoadBalancer(makeDefaultConfig());

  setSelectedEndpointsMetadata("envoy.lb", R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value: { string_value: "127.0.0.1:80" }
    }
  )pb");
  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  HostConstSharedPtr host = load_balancer_->chooseHost(&load_balancer_context_).host;
  EXPECT_EQ(host->address()->asString(), "127.0.0.1:80");
}

TEST_F(DynamicForwardingLoadBalancerTest, SelectEndpointUsingMetadataMissingEndpoint) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {Envoy::Upstream::makeTestHost(
      cluster_info_, "tcp://127.0.0.1:80", server_factory_context_.time_system_, us_central1_a, 1,
      0, Host::HealthStatus::HEALTHY)};
  host_set->hosts_per_locality_ = ::Envoy::Upstream::makeHostsPerLocality({{host_set->hosts_[0]}});
  makeCrossPriorityHostMap();

  createLoadBalancer(makeDefaultConfig());

  setSelectedEndpointsMetadata("envoy.lb", R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value: { string_value: "1.2.3.4:80" }
    }
  )pb");
  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  // In case primary endpoint is not found, fallback LB policy is used.
  EXPECT_NE(load_balancer_->chooseHost(&load_balancer_context_).host, nullptr);
}

TEST_F(DynamicForwardingLoadBalancerTest, SelectIPv6UsingMetadata) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");
  Locality us_central1_b = makeLocality("us-central1", "us-central1-b");
  Locality us_west3_c = makeLocality("us-west3", "us-west3-c");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[fda3:e722:ac3:cc00:172:b9fb:a00:2]:80",
                                    server_factory_context_.time_system_, us_central1_a, 1, 0,
                                    Host::HealthStatus::HEALTHY),
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[fda3:e722:ac3:cc00:172:b9fb:a00:3]:80",
                                    server_factory_context_.time_system_, us_central1_b, 1, 0,
                                    Host::HealthStatus::UNHEALTHY),
      Envoy::Upstream::makeTestHost(cluster_info_, "tcp://[fda3:e722:ac3:cc00:172:b9fb:a00:4]:80",
                                    server_factory_context_.time_system_, us_west3_c, 1, 0,
                                    Host::HealthStatus::DEGRADED)};
  host_set->hosts_per_locality_ = ::Envoy::Upstream::makeHostsPerLocality(
      {{host_set->hosts_[0]}, {host_set->hosts_[1]}, {host_set->hosts_[2]}});
  makeCrossPriorityHostMap();

  createLoadBalancer(makeDefaultConfig());

  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));

  setSelectedEndpointsMetadata("envoy.lb", R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value: { string_value: "[fda3:e722:ac3:cc00:172:b9fb:a00:3]:80" }
    }
  )pb");
  // Health status is not currently checked
  HostConstSharedPtr host = load_balancer_->chooseHost(&load_balancer_context_).host;
  EXPECT_EQ(host->address()->asString(), "[fda3:e722:ac3:cc00:172:b9fb:a00:3]:80");
}

TEST_F(DynamicForwardingLoadBalancerTest, SelectEndpointBadMetadata) {
  Locality us_central1_a = makeLocality("us-central1", "us-central1-a");

  MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(0);
  host_set->hosts_ = {Envoy::Upstream::makeTestHost(
      cluster_info_, "tcp://127.0.0.1:80", server_factory_context_.time_system_, us_central1_a, 1,
      0, Host::HealthStatus::HEALTHY)};
  host_set->hosts_per_locality_ = ::Envoy::Upstream::makeHostsPerLocality({{host_set->hosts_[0]}});
  makeCrossPriorityHostMap();

  createLoadBalancer(makeDefaultConfig());

  // Use wrong value type.
  setSelectedEndpointsMetadata("envoy.lb", R"pb(
    fields {
      key: "x-gateway-destination-endpoint"
      value: { number_value: 123 }
    }
  )pb");
  EXPECT_CALL(stream_info_, dynamicMetadata()).WillRepeatedly(ReturnRef(metadata_));
  // Even though metadata is invalid, the fallback LB will be used to select a host.
  EXPECT_NE(load_balancer_->chooseHost(&load_balancer_context_).host, nullptr);
}

} // namespace
} // namespace DynamicForwarding
} // namespace LoadBalancingPolices
} // namespace Extensions
} // namespace Envoy
