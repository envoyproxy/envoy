#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/router/rds_impl.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

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

    Json::ObjectPtr config = Json::Factory::LoadFromString(config_json);

    interval_timer_ = new Event::MockTimer(&dispatcher_);
    expectRequest();
    rds_ = RouteConfigProviderUtil::create(*config, runtime_, cm_, dispatcher_, random_,
                                           local_info_, store_, "foo.", tls_);
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
  Upstream::MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::IsolatedStoreImpl store_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  Http::MockAsyncClientRequest request_;
  RouteConfigProviderPtr rds_;
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

  Json::ObjectPtr config = Json::Factory::LoadFromString(config_json);
  EXPECT_THROW(RouteConfigProviderUtil::create(*config, runtime_, cm_, dispatcher_, random_,
                                               local_info_, store_, "foo.", tls_),
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

  Json::ObjectPtr config = Json::Factory::LoadFromString(config_json);
  local_info_.cluster_name_ = "";
  local_info_.node_name_ = "";
  interval_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_THROW(RouteConfigProviderUtil::create(*config, runtime_, cm_, dispatcher_, random_,
                                               local_info_, store_, "foo.", tls_),
               EnvoyException);
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
  message->body(Buffer::InstancePtr{new Buffer::OwnedImpl(response1_json)});

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(nullptr, rds_->config()->route(Http::TestHeaderMapImpl{{":authority", "foo"}}, 0));

  expectRequest();
  interval_timer_->callback_();

  // 2nd request with same response. Based on hash should not reload config.
  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body(Buffer::InstancePtr{new Buffer::OwnedImpl(response1_json)});

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(nullptr, rds_->config()->route(Http::TestHeaderMapImpl{{":authority", "foo"}}, 0));

  expectRequest();
  interval_timer_->callback_();

  // Load the config and verified shared count.
  ConfigPtr config = rds_->config();
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
  message->body(Buffer::InstancePtr{new Buffer::OwnedImpl(response2_json)});

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

  std::string response1_json = R"EOF(
  {
    "blah": true
  }
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body(Buffer::InstancePtr{new Buffer::OwnedImpl(response1_json)});

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  expectRequest();
  interval_timer_->callback_();

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(2UL, store_.counter("foo.rds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("foo.rds.update_failure").value());
}

} // Upstream
