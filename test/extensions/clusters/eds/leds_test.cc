#include <memory>

#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/config/utility.h"
#include "source/common/config/xds_resource.h"
#include "source/common/singleton/manager_impl.h"
#include "source/extensions/clusters/eds/leds.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/protobuf/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/admin.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Eq;

namespace Envoy {
namespace Upstream {
namespace {

class LedsTest : public testing::Test {
public:
  // Builds an LbEndpoint proto object.
  static envoy::config::endpoint::v3::LbEndpoint buildLbEndpoint(const std::string& address,
                                                                 uint32_t port) {
    return TestUtility::parseYaml<envoy::config::endpoint::v3::LbEndpoint>(fmt::format(R"EOF(
      endpoint:
        address:
          socket_address:
            address: {}
            port_value: {}
      )EOF",
                                                                                       address,
                                                                                       port));
  }

  // Returns a list of added Resource objects as is being returned in a delta-xDS response.
  static Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource>
  buildAddedResources(const std::vector<envoy::config::endpoint::v3::LbEndpoint>& added_or_updated,
                      const std::vector<std::string>& resources_names) {
    Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> to_ret;

    ASSERT(added_or_updated.size() == resources_names.size());

    for (size_t idx = 0; idx < added_or_updated.size(); ++idx) {
      const auto& lb_endpoint = added_or_updated[idx];
      const auto& resource_name = resources_names[idx];
      auto* resource = to_ret.Add();
      resource->set_name(resource_name);
      resource->set_version("1");
      resource->mutable_resource()->PackFrom(lb_endpoint);
    }

    return to_ret;
  }

  // Returns a list of removed resource names as is being returned in a delta-xDS response.
  static Protobuf::RepeatedPtrField<std::string>
  buildRemovedResources(const std::vector<std::string>& removed) {
    return Protobuf::RepeatedPtrField<std::string>{removed.begin(), removed.end()};
  }

  // Creates a leds configuration given a YAML string.
  static envoy::config::endpoint::v3::LedsClusterLocalityConfig
  makeLedsConfiguration(absl::string_view leds_config_yaml) {
    // Set the LEDS config.
    envoy::config::endpoint::v3::LedsClusterLocalityConfig leds_config;
    TestUtility::loadFromYaml(std::string(leds_config_yaml), leds_config);

    return leds_config;
  }

  void initialize() { initialize(makeLedsConfiguration(DEFAULT_LEDS_CONFIG_YAML)); }

  void initialize(const envoy::config::endpoint::v3::LedsClusterLocalityConfig& leds_config) {
    server_context_.local_info_.node_.mutable_locality()->set_zone("us-east-1a");

    cluster_scope_ = stats_.createScope("cluster.xds_cluster.");
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        server_context_, ssl_context_manager_, *cluster_scope_, server_context_.cluster_manager_,
        stats_, validation_visitor_);

    // Setup LEDS subscription.
    EXPECT_CALL(server_context_.cluster_manager_.subscription_factory_,
                collectionSubscriptionFromUrl(
                    _, _,
                    Eq(envoy::config::endpoint::v3::LbEndpoint().GetDescriptor()->full_name()), _,
                    _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke(
            [this, &leds_config](const xds::core::v3::ResourceLocator& locator_url,
                                 const envoy::config::core::v3::ConfigSource&, absl::string_view,
                                 Stats::Scope&, Envoy::Config::SubscriptionCallbacks& callbacks,
                                 Envoy::Config::OpaqueResourceDecoderSharedPtr) {
              // Verify that the locator is correct.
              Config::XdsResourceIdentifier::EncodeOptions encode_options;
              encode_options.sort_context_params_ = true;
              EXPECT_EQ(leds_config.leds_collection_name(),
                        Config::XdsResourceIdentifier::encodeUrl(locator_url, encode_options));
              // Set the callbacks, and verify that start() is called correctly.
              auto ret = std::make_unique<NiceMock<Envoy::Config::MockSubscription>>();
              leds_callbacks_ = &callbacks;
              EXPECT_CALL(*ret, start(_))
                  .WillOnce(Invoke([](const absl::flat_hash_set<std::string>& resource_names) {
                    // No resource names for a glob collection.
                    EXPECT_EQ(resource_names.size(), 0);
                  }));
              return ret;
            }));

    leds_subscription_ = std::make_unique<LedsSubscription>(leds_config, "xds_cluster",
                                                            factory_context, *cluster_scope_.get(),
                                                            [&]() { callbacks_called_counter_++; });
  }

  static void compareEndpointsMapContents(
      const LedsSubscription::LbEndpointsMap& actual_map,
      const std::vector<std::pair<std::string, envoy::config::endpoint::v3::LbEndpoint>>&
          expected_contents) {
    EXPECT_EQ(actual_map.size(), expected_contents.size());
    for (const auto& [resource_name, proto_value] : expected_contents) {
      const auto map_it = actual_map.find(resource_name);
      EXPECT_TRUE(map_it != actual_map.end());
      EXPECT_THAT(proto_value, ProtoEq(map_it->second));
    }
  }

  static constexpr absl::string_view DEFAULT_LEDS_CONFIG_YAML{R"EOF(
    leds_config:
      api_config_source:
        api_type: DELTA_GRPC
        grpc_services:
          envoy_grpc:
            cluster_name: xds_cluster
      resource_api_version: V3
    leds_collection_name: xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/*
  )EOF"};

  // Number of times the LEDS subscription callback was called.
  NiceMock<Server::Configuration::MockServerFactoryContext> server_context_;
  uint32_t callbacks_called_counter_{0};
  Stats::TestUtil::TestStore stats_;
  Ssl::MockContextManager ssl_context_manager_;
  Envoy::Stats::ScopeSharedPtr cluster_scope_;
  LedsSubscriptionPtr leds_subscription_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;

  Config::SubscriptionCallbacks* leds_callbacks_{};
};

// Verify that a successful onConfigUpdate() calls the callback, and has all the
// endpoints.
TEST_F(LedsTest, OnConfigUpdateSuccess) {
  initialize();
  const auto lb_endpoint = buildLbEndpoint("127.0.0.1", 12345);
  const auto& added_resources = buildAddedResources(
      {lb_endpoint}, {"xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/endpoint0"});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::endpoint::v3::LbEndpoint>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  leds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed_resources, "");
  EXPECT_EQ(1UL, callbacks_called_counter_);
  EXPECT_TRUE(leds_subscription_->isUpdated());
  const auto& all_endpoints_map = leds_subscription_->getEndpointsMap();
  EXPECT_EQ(1UL, all_endpoints_map.size());
  EXPECT_TRUE(TestUtility::protoEqual(lb_endpoint, all_endpoints_map.begin()->second));
}

// Verify that onConfigUpdate() with empty LbEndpoints vector size ignores config.
TEST_F(LedsTest, OnConfigUpdateEmpty) {
  initialize();
  EXPECT_FALSE(leds_subscription_->isUpdated());
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  leds_callbacks_->onConfigUpdate({}, removed_resources, "");
  EXPECT_EQ(1UL, stats_.counter("cluster.xds_cluster.leds.update_empty").value());
  // Verify that the callback was called even after an empty update.
  EXPECT_EQ(1UL, callbacks_called_counter_);
  EXPECT_TRUE(leds_subscription_->isUpdated());

  // Verify that the second time an empty update arrives, the callback isn't called.
  leds_callbacks_->onConfigUpdate({}, removed_resources, "");
  EXPECT_EQ(2UL, stats_.counter("cluster.xds_cluster.leds.update_empty").value());
  EXPECT_EQ(1UL, callbacks_called_counter_);
}

// Verify that onConfigUpdateFailed() calls the callback.
TEST_F(LedsTest, OnConfigUpdateFailed) {
  initialize();
  EXPECT_FALSE(leds_subscription_->isUpdated());
  const std::unique_ptr<EnvoyException> ex = std::make_unique<EnvoyException>("Update Failed");
  leds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                        ex.get());
  // Verify that the callback was called even after an failed update.
  EXPECT_EQ(1UL, callbacks_called_counter_);
  EXPECT_TRUE(leds_subscription_->isUpdated());
  EXPECT_EQ(0UL, leds_subscription_->getEndpointsMap().size());
}

// Verify that onConfigUpdateFailed() doesn't change the endpoints.
TEST_F(LedsTest, OnConfigUpdateFailedEndpoints) {
  initialize();
  // Add an endpoint.
  const auto lb_endpoint = buildLbEndpoint("127.0.0.1", 12345);
  const auto& added_resources = buildAddedResources(
      {lb_endpoint}, {"xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/endpoint0"});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::endpoint::v3::LbEndpoint>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  leds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed_resources, "");
  EXPECT_EQ(1UL, callbacks_called_counter_);

  // Verify there's an endpoint.
  const auto& all_endpoints_map = leds_subscription_->getEndpointsMap();
  EXPECT_EQ(1UL, all_endpoints_map.size());
  EXPECT_TRUE(TestUtility::protoEqual(lb_endpoint, all_endpoints_map.begin()->second));

  // Fail the config.
  const std::unique_ptr<EnvoyException> ex = std::make_unique<EnvoyException>("Update Failed");
  leds_callbacks_->onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason::FetchTimedout,
                                        ex.get());
  EXPECT_EQ(2UL, callbacks_called_counter_);

  // Verify that the same endpoint exists.
  const auto& all_endpoints_map2 = leds_subscription_->getEndpointsMap();
  EXPECT_EQ(1UL, all_endpoints_map2.size());
  EXPECT_TRUE(TestUtility::protoEqual(lb_endpoint, all_endpoints_map2.begin()->second));
}

// Verify the update of an endpoint.
TEST_F(LedsTest, UpdateEndpoint) {
  initialize();
  // Add 2 endpoints.
  const std::string lb_endpoint1_name{
      "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/endpoint0"};
  const std::string lb_endpoint2_name{
      "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/endpoint1"};
  const auto lb_endpoint1 = buildLbEndpoint("127.0.0.1", 12345);
  const auto lb_endpoint2 = buildLbEndpoint("127.0.0.1", 54321);
  const auto& added_resources =
      buildAddedResources({lb_endpoint1, lb_endpoint2}, {lb_endpoint1_name, lb_endpoint2_name});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::endpoint::v3::LbEndpoint>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  leds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed_resources, "");
  EXPECT_EQ(1UL, callbacks_called_counter_);
  EXPECT_TRUE(leds_subscription_->isUpdated());
  const auto& all_endpoints_map = leds_subscription_->getEndpointsMap();
  EXPECT_EQ(2UL, all_endpoints_map.size());
  compareEndpointsMapContents(
      all_endpoints_map, {{lb_endpoint1_name, lb_endpoint1}, {lb_endpoint2_name, lb_endpoint2}});

  // Update the first endpoint.
  const auto lb_endpoint1_update = buildLbEndpoint("127.0.0.1", 12346);
  const auto& updated_resources = buildAddedResources({lb_endpoint1_update}, {lb_endpoint1_name});
  const auto decoded_resources_update =
      TestUtility::decodeResources<envoy::config::endpoint::v3::LbEndpoint>(updated_resources);
  leds_callbacks_->onConfigUpdate(decoded_resources_update.refvec_, removed_resources, "");
  EXPECT_EQ(2UL, callbacks_called_counter_);
  EXPECT_TRUE(leds_subscription_->isUpdated());
  const auto& all_endpoints_update = leds_subscription_->getEndpointsMap();
  EXPECT_EQ(2UL, all_endpoints_update.size());
  compareEndpointsMapContents(all_endpoints_map, {{lb_endpoint1_name, lb_endpoint1_update},
                                                  {lb_endpoint2_name, lb_endpoint2}});
}

// Verify adding 2 endpoints then removing one.
TEST_F(LedsTest, RemoveEndpoint) {
  initialize();
  // Add 2 endpoints.
  const auto lb_endpoint1_name{
      "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/endpoint0"};
  const auto lb_endpoint2_name{
      "xdstp://test/envoy.config.endpoint.v3.LbEndpoint/foo-endpoints/endpoint1"};
  const auto lb_endpoint1 = buildLbEndpoint("127.0.0.1", 12345);
  const auto lb_endpoint2 = buildLbEndpoint("127.0.0.1", 54321);
  const auto& added_resources =
      buildAddedResources({lb_endpoint1, lb_endpoint2}, {lb_endpoint1_name, lb_endpoint2_name});
  const auto decoded_resources =
      TestUtility::decodeResources<envoy::config::endpoint::v3::LbEndpoint>(added_resources);
  const Protobuf::RepeatedPtrField<std::string> removed_resources;
  leds_callbacks_->onConfigUpdate(decoded_resources.refvec_, removed_resources, "");
  EXPECT_EQ(1UL, callbacks_called_counter_);
  EXPECT_TRUE(leds_subscription_->isUpdated());
  const auto& all_endpoints_map = leds_subscription_->getEndpointsMap();
  EXPECT_EQ(2UL, all_endpoints_map.size());
  compareEndpointsMapContents(
      all_endpoints_map, {{lb_endpoint1_name, lb_endpoint1}, {lb_endpoint2_name, lb_endpoint2}});

  // Remove the first endpoint.
  const auto& removed_resources_update = buildRemovedResources({lb_endpoint1_name});
  leds_callbacks_->onConfigUpdate({}, removed_resources_update, "");
  EXPECT_EQ(2UL, callbacks_called_counter_);
  EXPECT_TRUE(leds_subscription_->isUpdated());
  const auto& all_endpoints_update = leds_subscription_->getEndpointsMap();
  EXPECT_EQ(1UL, all_endpoints_update.size());
  compareEndpointsMapContents(all_endpoints_map, {{lb_endpoint2_name, lb_endpoint2}});
}

} // namespace
} // namespace Upstream
} // namespace Envoy
