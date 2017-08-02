#include <chrono>
#include <string>

#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/router/rds_impl.h"

#include "test/mocks/init/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Router {

class RdsImplTest : public testing::Test {
public:
  RdsImplTest() : request_(&cm_.async_client_) {}
  ~RdsImplTest() { tls_.shutdownThread(); }

  void setup() {
    std::string config_json = R"EOF(
    {
      "rds": {
        "cluster": "foo_cluster",
        "route_config_name": "foo_route_config",
        "refresh_delay_ms": 1000
      }
    }
    )EOF";

    Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);

    interval_timer_ = new Event::MockTimer(&dispatcher_);
    EXPECT_CALL(init_manager_, registerTarget(_));
    rds_ = RouteConfigProviderUtil::create(*config, runtime_, cm_, store_, "foo.", init_manager_,
                                           http_route_manager_);
    expectRequest();
    init_manager_.initialize();
  }

  void expectRequest() {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("foo_cluster"));
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(
            Invoke([&](Http::MessagePtr& request, Http::AsyncClient::Callbacks& callbacks,
                       const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
              EXPECT_EQ((Http::TestHeaderMapImpl{
                            {":method", "GET"},
                            {":path", "/v1/routes/foo_route_config/cluster_name/node_name"},
                            {":authority", "foo_cluster"}}),
                        request->headers());
              callbacks_ = &callbacks;
              return &request_;
            }));
  }

  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::IsolatedStoreImpl store_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Init::MockManager> init_manager_;
  Http::MockAsyncClientRequest request_;
  HttpRouteManagerImpl http_route_manager_{runtime_, dispatcher_, random_, local_info_, tls_};
  RouteConfigProviderSharedPtr rds_;
  Event::MockTimer* interval_timer_{};
  Http::AsyncClient::Callbacks* callbacks_{};
};

TEST_F(RdsImplTest, RdsAndStatic) {
  std::string config_json = R"EOF(
    {
      "rds": {},
      "route_config": {}
    }
    )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);
  EXPECT_THROW(RouteConfigProviderUtil::create(*config, runtime_, cm_, store_, "foo.",
                                               init_manager_, http_route_manager_),
               EnvoyException);
}

TEST_F(RdsImplTest, LocalInfoNotDefined) {
  std::string config_json = R"EOF(
    {
      "rds": {
        "cluster": "foo_cluster",
        "route_config_name": "foo_route_config"
      }
    }
    )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);
  local_info_.cluster_name_ = "";
  local_info_.node_name_ = "";
  interval_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_THROW(RouteConfigProviderUtil::create(*config, runtime_, cm_, store_, "foo.",
                                               init_manager_, http_route_manager_),
               EnvoyException);
}

TEST_F(RdsImplTest, UnknownCluster) {
  std::string config_json = R"EOF(
    {
      "rds": {
        "cluster": "foo_cluster",
        "route_config_name": "foo_route_config"
      }
    }
    )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);
  ON_CALL(cm_, get("foo_cluster")).WillByDefault(Return(nullptr));
  interval_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_THROW(RouteConfigProviderUtil::create(*config, runtime_, cm_, store_, "foo.",
                                               init_manager_, http_route_manager_),
               EnvoyException);
}

TEST_F(RdsImplTest, DestroyDuringInitialize) {
  InSequence s;

  setup();
  EXPECT_CALL(init_manager_.initialized_, ready());
  EXPECT_CALL(request_, cancel());
  rds_.reset();
}

TEST_F(RdsImplTest, Basic) {
  InSequence s;

  setup();

  // Make sure the initial empty route table works.
  EXPECT_EQ(nullptr, rds_->config()->route(Http::TestHeaderMapImpl{{":authority", "foo"}}, 0));

  // Initial request.
  std::string response1_json = R"EOF(
  {
    "virtual_hosts": []
  }
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response1_json));

  EXPECT_CALL(init_manager_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(nullptr, rds_->config()->route(Http::TestHeaderMapImpl{{":authority", "foo"}}, 0));

  expectRequest();
  interval_timer_->callback_();

  // 2nd request with same response. Based on hash should not reload config.
  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response1_json));

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(nullptr, rds_->config()->route(Http::TestHeaderMapImpl{{":authority", "foo"}}, 0));

  expectRequest();
  interval_timer_->callback_();

  // Load the config and verified shared count.
  ConfigConstSharedPtr config = rds_->config();
  EXPECT_EQ(2, config.use_count());

  // Third request.
  std::string response2_json = R"EOF(
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
  EXPECT_CALL(cm_, get("bar")).Times(0);
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ("foo", rds_->config()
                       ->route(Http::TestHeaderMapImpl{{":authority", "foo"}, {":path", "/foo"}}, 0)
                       ->routeEntry()
                       ->clusterName());

  // Old config use count should be 1 now.
  EXPECT_EQ(1, config.use_count());

  EXPECT_EQ(2UL, store_.counter("foo.rds.config_reload").value());
  EXPECT_EQ(3UL, store_.counter("foo.rds.update_attempt").value());
  EXPECT_EQ(3UL, store_.counter("foo.rds.update_success").value());
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

  EXPECT_CALL(init_manager_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  expectRequest();
  interval_timer_->callback_();

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(2UL, store_.counter("foo.rds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("foo.rds.update_failure").value());
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

  EXPECT_CALL(init_manager_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ(1UL, store_.counter("foo.rds.update_attempt").value());
  EXPECT_EQ(1UL, store_.counter("foo.rds.update_failure").value());
}

class HttpRouteManagerImplTest : public testing::Test {
public:
  HttpRouteManagerImplTest() {}
  ~HttpRouteManagerImplTest() {}

  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Upstream::MockClusterManager> cm_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::IsolatedStoreImpl store_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Init::MockManager> init_manager_;
  HttpRouteManagerImpl http_route_manager_{runtime_, dispatcher_, random_, local_info_, tls_};
};

TEST_F(HttpRouteManagerImplTest, Basic) {
  init_manager_.initialize();

  std::string config_json = R"EOF(
    {
      "cluster": "foo_cluster",
      "route_config_name": "foo_route_config",
      "refresh_delay_ms": 1000
    }
    )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);

  // Get a RouteConfigProvider. This one should create an entry in the HttpRouteManager.
  RouteConfigProviderSharedPtr provider =
      http_route_manager_.getRouteConfigProvider(*config, cm_, store_, "foo_prefix", init_manager_);
  // Because this get has the same cluster and route_config_name, the provider returned is just a
  // shared_ptr to the same provider as the one above.
  RouteConfigProviderSharedPtr provider2 =
      http_route_manager_.getRouteConfigProvider(*config, cm_, store_, "foo_prefix", init_manager_);
  // So this means that both shared_ptrs should be the same.
  EXPECT_EQ(provider, provider2);

  std::string config_json2 = R"EOF(
    {
      "cluster": "bar_cluster",
      "route_config_name": "foo_route_config",
      "refresh_delay_ms": 1000
    }
    )EOF";

  Json::ObjectSharedPtr config2 = Json::Factory::loadFromString(config_json2);

  RouteConfigProviderSharedPtr provider3 = http_route_manager_.getRouteConfigProvider(
      *config2, cm_, store_, "foo_prefix", init_manager_);
  EXPECT_NE(provider3, provider);

  std::vector<RouteConfigProviderSharedPtr> configured_providers =
      http_route_manager_.routeConfigProviders();
  EXPECT_EQ(2UL, configured_providers.size());

  provider.reset();
  provider2.reset();
  configured_providers.clear();

  // All shared_ptrs to the provider pointed at by provider1, and provider2 have been deleted, so
  // now we should only have the provider porinted at by provider3.
  configured_providers = http_route_manager_.routeConfigProviders();
  EXPECT_EQ(1UL, configured_providers.size());
  EXPECT_EQ(provider3, configured_providers.front());

  provider3.reset();
  configured_providers.clear();

  configured_providers = http_route_manager_.routeConfigProviders();
  EXPECT_EQ(0UL, configured_providers.size());
}

} // namespace Router
} // namespace Envoy
