#include <chrono>
#include <string>

#include "envoy/admin/v2alpha/config_dump.pb.h"
#include "envoy/admin/v2alpha/config_dump.pb.validate.h"

#include "common/config/filter_json.h"
#include "common/config/utility.h"
#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/router/rds_impl.h"

#include "server/http/admin.h"

#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::_;

namespace Envoy {
namespace Router {
namespace {

envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager
parseHttpConnectionManagerFromJson(const std::string& json_string) {
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager
      http_connection_manager;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Envoy::Config::FilterJson::translateHttpConnectionManager(*json_object_ptr,
                                                            http_connection_manager);
  return http_connection_manager;
}

class RdsTestBase : public testing::Test {
public:
  RdsTestBase() : request_(&factory_context_.cluster_manager_.async_client_) {}

  void expectRequest() {
    EXPECT_CALL(factory_context_.cluster_manager_, httpAsyncClientForCluster("foo_cluster"));
    EXPECT_CALL(factory_context_.cluster_manager_.async_client_, send_(_, _, _))
        .WillOnce(Invoke(
            [&](Http::MessagePtr& request, Http::AsyncClient::Callbacks& callbacks,
                const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
              EXPECT_EQ((Http::TestHeaderMapImpl{
                            {":method", "GET"},
                            {":path", "/v1/routes/foo_route_config/cluster_name/node_name"},
                            {":authority", "foo_cluster"}}),
                        request->headers());
              callbacks_ = &callbacks;
              return &request_;
            }));
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Http::MockAsyncClientRequest request_;
  Http::AsyncClient::Callbacks* callbacks_{};
  Event::MockTimer* interval_timer_{};
};

class RdsImplTest : public RdsTestBase {
public:
  RdsImplTest() {
    EXPECT_CALL(factory_context_.admin_.config_tracker_, add_("routes", _));
    route_config_provider_manager_.reset(
        new RouteConfigProviderManagerImpl(factory_context_.admin_));
  }
  ~RdsImplTest() { factory_context_.thread_local_.shutdownThread(); }

  void setup() {
    const std::string config_json = R"EOF(
    {
      "rds": {
        "cluster": "foo_cluster",
        "route_config_name": "foo_route_config",
        "refresh_delay_ms": 1000
      },
      "codec_type": "auto",
      "stat_prefix": "foo",
      "filters": [
        { "name": "http_dynamo_filter", "config": {} }
      ]
    }
    )EOF";

    Upstream::ClusterManager::ClusterInfoMap cluster_map;
    Upstream::MockCluster cluster;
    cluster_map.emplace("foo_cluster", cluster);
    EXPECT_CALL(factory_context_.cluster_manager_, clusters()).WillOnce(Return(cluster_map));
    EXPECT_CALL(cluster, info());
    EXPECT_CALL(*cluster.info_, addedViaApi());
    EXPECT_CALL(cluster, info());
    EXPECT_CALL(*cluster.info_, type());
    interval_timer_ = new Event::MockTimer(&factory_context_.dispatcher_);
    EXPECT_CALL(factory_context_.init_manager_, registerTarget(_));
    rds_ =
        RouteConfigProviderUtil::create(parseHttpConnectionManagerFromJson(config_json),
                                        factory_context_, "foo.", *route_config_provider_manager_);
    expectRequest();
    factory_context_.init_manager_.initialize();
  }

  NiceMock<Server::MockInstance> server_;
  std::unique_ptr<RouteConfigProviderManagerImpl> route_config_provider_manager_;
  RouteConfigProviderSharedPtr rds_;
};

TEST_F(RdsImplTest, RdsAndStatic) {
  const std::string config_json = R"EOF(
    {
      "rds": {},
      "route_config": {},
      "codec_type": "auto",
      "stat_prefix": "foo",
      "filters": [
        { "name": "http_dynamo_filter", "config": {} }
      ]
    }
    )EOF";

  EXPECT_THROW(RouteConfigProviderUtil::create(parseHttpConnectionManagerFromJson(config_json),
                                               factory_context_, "foo.",
                                               *route_config_provider_manager_),
               EnvoyException);
}

TEST_F(RdsImplTest, LocalInfoNotDefined) {
  const std::string config_json = R"EOF(
    {
      "rds": {
        "cluster": "foo_cluster",
        "route_config_name": "foo_route_config"
      },
      "codec_type": "auto",
      "stat_prefix": "foo",
      "filters": [
        { "name": "http_dynamo_filter", "config": {} }
      ]
    }
    )EOF";

  factory_context_.local_info_.node_.set_cluster("");
  factory_context_.local_info_.node_.set_id("");
  EXPECT_THROW(RouteConfigProviderUtil::create(parseHttpConnectionManagerFromJson(config_json),
                                               factory_context_, "foo.",
                                               *route_config_provider_manager_),
               EnvoyException);
}

TEST_F(RdsImplTest, UnknownCluster) {
  const std::string config_json = R"EOF(
    {
      "rds": {
        "cluster": "foo_cluster",
        "route_config_name": "foo_route_config"
      },
      "codec_type": "auto",
      "stat_prefix": "foo",
      "filters": [
        { "name": "http_dynamo_filter", "config": {} }
      ]
    }
    )EOF";

  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  EXPECT_CALL(factory_context_.cluster_manager_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_THROW_WITH_MESSAGE(
      RouteConfigProviderUtil::create(parseHttpConnectionManagerFromJson(config_json),
                                      factory_context_, "foo.", *route_config_provider_manager_),
      EnvoyException,
      "envoy::api::v2::core::ConfigSource must have a statically defined non-EDS "
      "cluster: 'foo_cluster' does not exist, was added via api, or is an "
      "EDS cluster");
}

TEST_F(RdsImplTest, DestroyDuringInitialize) {
  InSequence s;

  setup();
  EXPECT_CALL(factory_context_.init_manager_.initialized_, ready());
  EXPECT_CALL(request_, cancel());
  rds_.reset();
}

TEST_F(RdsImplTest, Basic) {
  InSequence s;
  Buffer::OwnedImpl empty;
  Buffer::OwnedImpl data;

  setup();

  // Make sure the initial empty route table works.
  EXPECT_EQ(nullptr, rds_->config()->route(Http::TestHeaderMapImpl{{":authority", "foo"}}, 0));
  EXPECT_EQ(0UL, factory_context_.scope_.gauge("foo.rds.foo_route_config.version").value());

  // Initial request.
  const std::string response1_json = R"EOF(
  {
    "virtual_hosts": []
  }
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response1_json));

  EXPECT_CALL(factory_context_.init_manager_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(nullptr, rds_->config()->route(Http::TestHeaderMapImpl{{":authority", "foo"}}, 0));
  EXPECT_EQ(1580011435426663819U,
            factory_context_.scope_.gauge("foo.rds.foo_route_config.version").value());

  expectRequest();
  interval_timer_->callback_();

  // 2nd request with same response. Based on hash should not reload config.
  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response1_json));

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(nullptr, rds_->config()->route(Http::TestHeaderMapImpl{{":authority", "foo"}}, 0));

  EXPECT_EQ(1580011435426663819U,
            factory_context_.scope_.gauge("foo.rds.foo_route_config.version").value());

  expectRequest();
  interval_timer_->callback_();

  // Load the config and verified shared count.
  ConfigConstSharedPtr config = rds_->config();
  EXPECT_EQ(2, config.use_count());

  // Third request.
  const std::string response2_json = R"EOF(
  {
    "virtual_hosts": [
    {
      "name": "local_service",
      "domains": ["*"],
      "routes": [
        {
          "prefix": "/foo",
          "cluster_header": ":authority"
        },
        {
          "prefix": "/bar",
          "cluster": "bar"
        }
      ]
    }
  ]
  }
  )EOF";

  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response2_json));

  // Make sure we don't lookup/verify clusters.
  EXPECT_CALL(factory_context_.cluster_manager_, get("bar")).Times(0);
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ("foo", rds_->config()
                       ->route(Http::TestHeaderMapImpl{{":authority", "foo"}, {":path", "/foo"}}, 0)
                       ->routeEntry()
                       ->clusterName());

  EXPECT_EQ(8808926191882896258U,
            factory_context_.scope_.gauge("foo.rds.foo_route_config.version").value());

  // Old config use count should be 1 now.
  EXPECT_EQ(1, config.use_count());

  EXPECT_EQ(2UL, factory_context_.scope_.counter("foo.rds.foo_route_config.config_reload").value());
  EXPECT_EQ(3UL,
            factory_context_.scope_.counter("foo.rds.foo_route_config.update_attempt").value());
  EXPECT_EQ(3UL,
            factory_context_.scope_.counter("foo.rds.foo_route_config.update_success").value());
  EXPECT_EQ(8808926191882896258U,
            factory_context_.scope_.gauge("foo.rds.foo_route_config.version").value());
}

TEST_F(RdsImplTest, Failure) {
  InSequence s;

  setup();

  std::string response_json = R"EOF(
  {
    "blah": true
  }
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response_json));

  EXPECT_CALL(factory_context_.init_manager_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  expectRequest();
  interval_timer_->callback_();

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(2UL,
            factory_context_.scope_.counter("foo.rds.foo_route_config.update_attempt").value());
  EXPECT_EQ(2UL,
            factory_context_.scope_.counter("foo.rds.foo_route_config.update_failure").value());
}

TEST_F(RdsImplTest, FailureArray) {
  InSequence s;

  setup();

  std::string response_json = R"EOF(
  []
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response_json));

  EXPECT_CALL(factory_context_.init_manager_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ(1UL,
            factory_context_.scope_.counter("foo.rds.foo_route_config.update_attempt").value());
  EXPECT_EQ(1UL,
            factory_context_.scope_.counter("foo.rds.foo_route_config.update_failure").value());
}

class RouteConfigProviderManagerImplTest : public RdsTestBase {
public:
  void setup() {
    std::string config_json = R"EOF(
      {
        "cluster": "foo_cluster",
        "route_config_name": "foo_route_config",
        "refresh_delay_ms": 1000
      }
      )EOF";

    Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);
    Envoy::Config::Utility::translateRdsConfig(*config, rds_);

    // Get a RouteConfigProvider. This one should create an entry in the RouteConfigProviderManager.
    Upstream::ClusterManager::ClusterInfoMap cluster_map;
    Upstream::MockCluster cluster;
    cluster_map.emplace("foo_cluster", cluster);
    EXPECT_CALL(factory_context_.cluster_manager_, clusters()).WillOnce(Return(cluster_map));
    EXPECT_CALL(cluster, info()).Times(2);
    EXPECT_CALL(*cluster.info_, addedViaApi());
    EXPECT_CALL(*cluster.info_, type());
    interval_timer_ = new Event::MockTimer(&factory_context_.dispatcher_);
    provider_ = route_config_provider_manager_->getRdsRouteConfigProvider(rds_, factory_context_,
                                                                          "foo_prefix.");
  }

  RouteConfigProviderManagerImplTest() {
    EXPECT_CALL(factory_context_.admin_.config_tracker_, add_("routes", _));
    route_config_provider_manager_.reset(
        new RouteConfigProviderManagerImpl(factory_context_.admin_));
  }

  ~RouteConfigProviderManagerImplTest() { factory_context_.thread_local_.shutdownThread(); }

  envoy::config::filter::network::http_connection_manager::v2::Rds rds_;
  std::unique_ptr<RouteConfigProviderManagerImpl> route_config_provider_manager_;
  RouteConfigProviderSharedPtr provider_;
};

envoy::api::v2::RouteConfiguration parseRouteConfigurationFromV2Yaml(const std::string& yaml) {
  envoy::api::v2::RouteConfiguration route_config;
  MessageUtil::loadFromYaml(yaml, route_config);
  return route_config;
}

TEST_F(RouteConfigProviderManagerImplTest, ConfigDump) {
  auto message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["routes"]();
  const auto& route_config_dump =
      MessageUtil::downcastAndValidate<const envoy::admin::v2alpha::RoutesConfigDump&>(
          *message_ptr);

  // No routes at all.
  envoy::admin::v2alpha::RoutesConfigDump expected_route_config_dump;
  MessageUtil::loadFromYaml(R"EOF(
static_route_configs:
dynamic_route_configs:
)EOF",
                            expected_route_config_dump);
  EXPECT_EQ(expected_route_config_dump.DebugString(), route_config_dump.DebugString());

  std::string config_yaml = R"EOF(
name: foo
virtual_hosts:
  - name: bar
    domains: ["*"]
    routes:
      - match: { prefix: "/" }
        route: { cluster: baz }
)EOF";

  // Only static route.
  RouteConfigProviderSharedPtr static_config =
      route_config_provider_manager_->getStaticRouteConfigProvider(
          parseRouteConfigurationFromV2Yaml(config_yaml), factory_context_);
  message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["routes"]();
  const auto& route_config_dump2 =
      MessageUtil::downcastAndValidate<const envoy::admin::v2alpha::RoutesConfigDump&>(
          *message_ptr);
  MessageUtil::loadFromYaml(R"EOF(
static_route_configs:
  - name: foo
    virtual_hosts:
      - name: bar
        domains: ["*"]
        routes:
          - match: { prefix: "/" }
            route: { cluster: baz }
dynamic_route_configs:
)EOF",
                            expected_route_config_dump);
  EXPECT_EQ(expected_route_config_dump.DebugString(), route_config_dump2.DebugString());

  // Static + dynamic.
  setup();
  expectRequest();
  factory_context_.init_manager_.initialize();
  const std::string response1_json = R"EOF(
  {
    "virtual_hosts": []
  }
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response1_json));
  EXPECT_CALL(factory_context_.init_manager_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  message_ptr = factory_context_.admin_.config_tracker_.config_tracker_callbacks_["routes"]();
  const auto& route_config_dump3 =
      MessageUtil::downcastAndValidate<const envoy::admin::v2alpha::RoutesConfigDump&>(
          *message_ptr);
  MessageUtil::loadFromYaml(R"EOF(
static_route_configs:
  - name: foo
    virtual_hosts:
      - name: bar
        domains: ["*"]
        routes:
          - match: { prefix: "/" }
            route: { cluster: baz }
dynamic_route_configs:
  - version_info: "hash_15ed54077da94d8b"
    route_config:
      name: foo_route_config
      virtual_hosts:
)EOF",
                            expected_route_config_dump);
  EXPECT_EQ(expected_route_config_dump.DebugString(), route_config_dump3.DebugString());
}

TEST_F(RouteConfigProviderManagerImplTest, Basic) {
  Buffer::OwnedImpl data;

  factory_context_.init_manager_.initialize();

  // Get a RouteConfigProvider. This one should create an entry in the RouteConfigProviderManager.
  setup();

  // Because this get has the same cluster and route_config_name, the provider returned is just a
  // shared_ptr to the same provider as the one above.
  RouteConfigProviderSharedPtr provider2 =
      route_config_provider_manager_->getRdsRouteConfigProvider(rds_, factory_context_,
                                                                "foo_prefix");
  // So this means that both shared_ptrs should be the same.
  EXPECT_EQ(provider_, provider2);
  EXPECT_EQ(2UL, provider_.use_count());

  std::string config_json2 = R"EOF(
    {
      "cluster": "bar_cluster",
      "route_config_name": "foo_route_config",
      "refresh_delay_ms": 1000
    }
    )EOF";

  Json::ObjectSharedPtr config2 = Json::Factory::loadFromString(config_json2);
  envoy::config::filter::network::http_connection_manager::v2::Rds rds2;
  Envoy::Config::Utility::translateRdsConfig(*config2, rds2);

  Upstream::ClusterManager::ClusterInfoMap cluster_map;
  Upstream::MockCluster cluster;
  cluster_map.emplace("bar_cluster", cluster);
  EXPECT_CALL(factory_context_.cluster_manager_, clusters()).WillOnce(Return(cluster_map));
  EXPECT_CALL(cluster, info()).Times(2);
  EXPECT_CALL(*cluster.info_, addedViaApi());
  EXPECT_CALL(*cluster.info_, type());
  new Event::MockTimer(&factory_context_.dispatcher_);
  RouteConfigProviderSharedPtr provider3 =
      route_config_provider_manager_->getRdsRouteConfigProvider(rds2, factory_context_,
                                                                "foo_prefix");
  EXPECT_NE(provider3, provider_);
  EXPECT_EQ(2UL, provider_.use_count());
  EXPECT_EQ(1UL, provider3.use_count());

  std::vector<RouteConfigProviderSharedPtr> configured_providers =
      route_config_provider_manager_->getRdsRouteConfigProviders();
  EXPECT_EQ(2UL, configured_providers.size());
  EXPECT_EQ(3UL, provider_.use_count());
  EXPECT_EQ(2UL, provider3.use_count());

  provider_.reset();
  provider2.reset();
  configured_providers.clear();

  // All shared_ptrs to the provider pointed at by provider1, and provider2 have been deleted, so
  // now we should only have the provider pointed at by provider3.
  configured_providers = route_config_provider_manager_->getRdsRouteConfigProviders();
  EXPECT_EQ(1UL, configured_providers.size());
  EXPECT_EQ(provider3, configured_providers.front());

  provider3.reset();
  configured_providers.clear();

  configured_providers = route_config_provider_manager_->getRdsRouteConfigProviders();
  EXPECT_EQ(0UL, configured_providers.size());
}

// Negative test for protoc-gen-validate constraints.
TEST_F(RouteConfigProviderManagerImplTest, ValidateFail) {
  setup();
  auto& provider_impl = dynamic_cast<RdsRouteConfigProviderImpl&>(*provider_.get());
  Protobuf::RepeatedPtrField<envoy::api::v2::RouteConfiguration> route_configs;
  auto* route_config = route_configs.Add();
  route_config->set_name("foo_route_config");
  route_config->mutable_virtual_hosts()->Add();
  EXPECT_THROW(provider_impl.onConfigUpdate(route_configs, ""), ProtoValidationException);
}

TEST_F(RouteConfigProviderManagerImplTest, onConfigUpdateEmpty) {
  setup();
  factory_context_.init_manager_.initialize();
  auto& provider_impl = dynamic_cast<RdsRouteConfigProviderImpl&>(*provider_.get());
  EXPECT_CALL(factory_context_.init_manager_.initialized_, ready());
  provider_impl.onConfigUpdate({}, "");
  EXPECT_EQ(
      1UL, factory_context_.scope_.counter("foo_prefix.rds.foo_route_config.update_empty").value());
}

TEST_F(RouteConfigProviderManagerImplTest, onConfigUpdateWrongSize) {
  setup();
  factory_context_.init_manager_.initialize();
  auto& provider_impl = dynamic_cast<RdsRouteConfigProviderImpl&>(*provider_.get());
  Protobuf::RepeatedPtrField<envoy::api::v2::RouteConfiguration> route_configs;
  route_configs.Add();
  route_configs.Add();
  EXPECT_CALL(factory_context_.init_manager_.initialized_, ready());
  EXPECT_THROW_WITH_MESSAGE(provider_impl.onConfigUpdate(route_configs, ""), EnvoyException,
                            "Unexpected RDS resource length: 2");
}

} // namespace
} // namespace Router
} // namespace Envoy
