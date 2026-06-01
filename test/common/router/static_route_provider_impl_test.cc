#include <chrono>
#include <memory>
#include <string>

#include "envoy/config/route/v3/route.pb.h"
#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"

#include "source/common/router/config_impl.h"
#include "source/common/router/route_config_update_receiver_impl.h"
#include "source/common/router/route_provider_manager.h"
#include "source/common/router/static_route_provider_impl.h"

#include "test/mocks/config/mocks.h"
#include "test/mocks/server/instance.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Router {
namespace {

using ProtoTraitsImpl =
    Rds::Common::ProtoTraitsImpl<envoy::config::route::v3::RouteConfiguration, 1>;

class StaticRouteConfigProviderImplTest : public testing::Test {
public:
  StaticRouteConfigProviderImplTest()
      : rds_manager_(server_factory_context_.admin_, "routes", proto_traits_) {
    ON_CALL(server_factory_context_, scope()).WillByDefault(ReturnRef(*scope_.rootScope()));
    ON_CALL(server_factory_context_, messageValidationContext())
        .WillByDefault(ReturnRef(validation_context_));
    EXPECT_CALL(validation_context_, dynamicValidationVisitor())
        .WillRepeatedly(ReturnRef(validation_visitor_));
  }

  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  NiceMock<ProtobufMessage::MockValidationContext> validation_context_;
  NiceMock<ProtobufMessage::MockValidationVisitor> validation_visitor_;
  NiceMock<Stats::MockIsolatedStatsStore> scope_;
  Event::SimulatedTimeSystem time_system_;
  ProtoTraitsImpl proto_traits_;
  ConfigTraitsImpl config_traits_{validation_visitor_};
  Rds::RouteConfigProviderManager rds_manager_;
};

// Validate that a static config without VHDS works.
TEST_F(StaticRouteConfigProviderImplTest, StaticConfigNoVhds) {
  const std::string config_yaml = R"EOF(
name: foo
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: baz }
)EOF";

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(config_yaml, route_config);

  server_factory_context_.cluster_manager_.initializeClusters({"baz"}, {});

  StaticRouteConfigProviderImpl provider(route_config, config_traits_, server_factory_context_,
                                         rds_manager_);

  EXPECT_EQ("foo", provider.configCast()->name());
  EXPECT_TRUE(provider.configInfo().has_value());
  EXPECT_NE(0, provider.lastUpdated().time_since_epoch().count());

  // Test requestVirtualHostsUpdate without VHDS should return immediately with false.
  bool called = false;
  auto cb = std::make_shared<Http::RouteConfigUpdatedCallback>([&called](bool exists) {
    called = true;
    EXPECT_FALSE(exists);
  });
  EXPECT_CALL(server_factory_context_.dispatcher_, post(_))
      .WillOnce(Invoke([](absl::AnyInvocable<void()> callback) { callback(); }));
  provider.requestVirtualHostsUpdate("example.com", server_factory_context_.dispatcher_, cb);
  EXPECT_TRUE(called);
}

// Validate that a static config with VHDS works.
TEST_F(StaticRouteConfigProviderImplTest, StaticConfigWithVhds) {
  const std::string config_yaml = R"EOF(
name: foo
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
)EOF";

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(config_yaml, route_config);

  // Setup for VHDS subscription.
  NiceMock<Envoy::Config::MockSubscriptionFactory> subscription_factory;
  ON_CALL(server_factory_context_.cluster_manager_, subscriptionFactory())
      .WillByDefault(ReturnRef(subscription_factory));

  Envoy::Config::SubscriptionCallbacks* vhds_callbacks = nullptr;
  auto subscription = std::make_unique<Envoy::Config::MockSubscription>();
  Envoy::Config::MockSubscription* subscription_ptr = subscription.get();
  EXPECT_CALL(subscription_factory, subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce(Invoke([&vhds_callbacks, &subscription](
                           const envoy::config::core::v3::ConfigSource&, absl::string_view,
                           Stats::Scope&, Envoy::Config::SubscriptionCallbacks& callbacks,
                           Envoy::Config::OpaqueResourceDecoderSharedPtr,
                           const Envoy::Config::SubscriptionOptions&) {
        vhds_callbacks = &callbacks;
        return absl::StatusOr<Envoy::Config::SubscriptionPtr>(std::move(subscription));
      }));

  StaticRouteConfigProviderImpl provider(route_config, config_traits_, server_factory_context_,
                                         rds_manager_);

  EXPECT_EQ("foo", provider.configCast()->name());
  EXPECT_TRUE(provider.configInfo().has_value());

  // Test requestVirtualHostsUpdate.
  bool cb_called = false;
  auto cb = std::make_shared<Http::RouteConfigUpdatedCallback>([&cb_called](bool exists) {
    cb_called = true;
    EXPECT_TRUE(exists);
  });

  EXPECT_CALL(*subscription_ptr, requestOnDemandUpdate(_));
  // First post to main thread.
  EXPECT_CALL(server_factory_context_.dispatcher_, post(_))
      .Times(2)
      .WillRepeatedly(Invoke([](absl::AnyInvocable<void()> callback) { callback(); }));
  provider.requestVirtualHostsUpdate("example.com", server_factory_context_.dispatcher_, cb);

  // Simulate a VHDS response.
  envoy::config::route::v3::VirtualHost vhost;
  vhost.set_name("example_vhost");
  vhost.add_domains("example.com");
  auto* route = vhost.add_routes();
  route->mutable_match()->set_prefix("/");
  route->mutable_route()->set_cluster("baz");

  server_factory_context_.cluster_manager_.initializeClusters({"baz"}, {});

  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> resources;
  auto* resource = resources.Add();
  resource->set_name("foo/example.com");
  resource->mutable_resource()->PackFrom(vhost);

  auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(resources, "name");

  // VhdsSubscription::onConfigUpdate will call provider.onConfigUpdate() which will post to worker
  // thread (Second post).
  EXPECT_TRUE(vhds_callbacks->onConfigUpdate(decoded_resources.refvec_, {}, "1").ok());

  EXPECT_TRUE(cb_called);
  auto config_impl = std::static_pointer_cast<const ConfigImpl>(provider.configCast());
  EXPECT_TRUE(config_impl->virtualHostExists(
      Http::TestRequestHeaderMapImpl{{":authority", "example.com"}}));
}

// Validates that multiple on-demand VHDS request with a static route works.
TEST_F(StaticRouteConfigProviderImplTest, MultipleOnDemandRequests) {
  const std::string config_yaml = R"EOF(
name: foo
vhds:
  config_source:
    api_config_source:
      api_type: DELTA_GRPC
      grpc_services:
        envoy_grpc:
          cluster_name: xds_cluster
)EOF";

  envoy::config::route::v3::RouteConfiguration route_config;
  TestUtility::loadFromYaml(config_yaml, route_config);

  NiceMock<Envoy::Config::MockSubscriptionFactory> subscription_factory;
  ON_CALL(server_factory_context_.cluster_manager_, subscriptionFactory())
      .WillByDefault(ReturnRef(subscription_factory));

  Envoy::Config::SubscriptionCallbacks* vhds_callbacks = nullptr;
  auto subscription = std::make_unique<Envoy::Config::MockSubscription>();
  Envoy::Config::MockSubscription* subscription_ptr = subscription.get();
  EXPECT_CALL(subscription_factory, subscriptionFromConfigSource(_, _, _, _, _, _))
      .WillOnce(Invoke([&vhds_callbacks, &subscription](
                           const envoy::config::core::v3::ConfigSource&, absl::string_view,
                           Stats::Scope&, Envoy::Config::SubscriptionCallbacks& callbacks,
                           Envoy::Config::OpaqueResourceDecoderSharedPtr,
                           const Envoy::Config::SubscriptionOptions&) {
        vhds_callbacks = &callbacks;
        return absl::StatusOr<Envoy::Config::SubscriptionPtr>(std::move(subscription));
      }));

  StaticRouteConfigProviderImpl provider(route_config, config_traits_, server_factory_context_,
                                         rds_manager_);

  // Request for example1.com.
  bool cb1_called = false;
  auto cb1 = std::make_shared<Http::RouteConfigUpdatedCallback>([&cb1_called](bool exists) {
    cb1_called = true;
    EXPECT_TRUE(exists);
  });

  EXPECT_CALL(*subscription_ptr,
              requestOnDemandUpdate(absl::flat_hash_set<std::string>{"foo/example1.com"}));
  EXPECT_CALL(server_factory_context_.dispatcher_, post(_))
      .WillOnce(Invoke([](absl::AnyInvocable<void()> callback) { callback(); }));
  provider.requestVirtualHostsUpdate("example1.com", server_factory_context_.dispatcher_, cb1);

  // Request for example2.com.
  bool cb2_called = false;
  auto cb2 = std::make_shared<Http::RouteConfigUpdatedCallback>([&cb2_called](bool exists) {
    cb2_called = true;
    EXPECT_TRUE(exists);
  });

  EXPECT_CALL(*subscription_ptr,
              requestOnDemandUpdate(absl::flat_hash_set<std::string>{"foo/example2.com"}));
  EXPECT_CALL(server_factory_context_.dispatcher_, post(_))
      .WillOnce(Invoke([](absl::AnyInvocable<void()> callback) { callback(); }));
  provider.requestVirtualHostsUpdate("example2.com", server_factory_context_.dispatcher_, cb2);

  // Simulate VHDS response for both example1.com and example2.com.
  Protobuf::RepeatedPtrField<envoy::service::discovery::v3::Resource> resources;

  envoy::config::route::v3::VirtualHost vhost1;
  vhost1.set_name("vhost1");
  vhost1.add_domains("example1.com");
  vhost1.add_routes()->mutable_match()->set_prefix("/");
  vhost1.mutable_routes(0)->mutable_route()->set_cluster("baz");

  auto* res1 = resources.Add();
  res1->set_name("foo/example1.com");
  res1->mutable_resource()->PackFrom(vhost1);

  envoy::config::route::v3::VirtualHost vhost2;
  vhost2.set_name("vhost2");
  vhost2.add_domains("example2.com");
  vhost2.add_routes()->mutable_match()->set_prefix("/");
  vhost2.mutable_routes(0)->mutable_route()->set_cluster("baz");

  auto* res2 = resources.Add();
  res2->set_name("foo/example2.com");
  res2->mutable_resource()->PackFrom(vhost2);

  server_factory_context_.cluster_manager_.initializeClusters({"baz"}, {});
  auto decoded_resources =
      TestUtility::decodeResources<envoy::config::route::v3::VirtualHost>(resources, "name");

  // onConfigUpdate will post to worker thread for both callbacks.
  EXPECT_CALL(server_factory_context_.dispatcher_, post(_))
      .Times(2)
      .WillRepeatedly(Invoke([](absl::AnyInvocable<void()> callback) { callback(); }));
  EXPECT_TRUE(vhds_callbacks->onConfigUpdate(decoded_resources.refvec_, {}, "1").ok());

  EXPECT_TRUE(cb1_called);
  EXPECT_TRUE(cb2_called);

  auto config_impl = std::static_pointer_cast<const ConfigImpl>(provider.configCast());
  EXPECT_TRUE(config_impl->virtualHostExists(
      Http::TestRequestHeaderMapImpl{{":authority", "example1.com"}}));
  EXPECT_TRUE(config_impl->virtualHostExists(
      Http::TestRequestHeaderMapImpl{{":authority", "example2.com"}}));
}

} // namespace
} // namespace Router
} // namespace Envoy
