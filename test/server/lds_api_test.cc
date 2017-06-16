#include "common/http/message_impl.h"

#include "server/lds_api.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::_;

namespace Envoy {
namespace Server {

class LdsApiTest : public testing::Test {
public:
  LdsApiTest() : request_(&cm_.async_client_) {}

  void setup() {
    std::string config_json = R"EOF(
    {
      "cluster": "foo_cluster",
      "refresh_delay_ms": 1000
    }
    )EOF";

    Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);
    EXPECT_CALL(init_, registerTarget(_));
    lds_.reset(new LdsApi(*config, cm_, dispatcher_, random_, init_, local_info_, store_, lm_));

    expectRequest();
    init_.initialize();
  }

  void expectAdd(const std::string& listener_name) {
    EXPECT_CALL(lm_, addOrUpdateListener(_))
        .WillOnce(Invoke([listener_name](const Json::Object& config) -> bool {
          EXPECT_EQ(listener_name, config.getString("name"));
          return true;
        }));
  }

  void expectRequest() {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("foo_cluster"));
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(
            Invoke([&](Http::MessagePtr& request, Http::AsyncClient::Callbacks& callbacks,
                       const Optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
              EXPECT_EQ((Http::TestHeaderMapImpl{{":method", "GET"},
                                                 {":path", "/v1/listeners/cluster_name/node_name"},
                                                 {":authority", "foo_cluster"}}),
                        request->headers());
              callbacks_ = &callbacks;
              return &request_;
            }));
  }

  void makeListenersAndExpectCall(const std::vector<std::string>& listener_names) {
    std::vector<std::reference_wrapper<Listener>> refs;
    listeners_.clear();
    for (const auto& name : listener_names) {
      listeners_.emplace_back();
      listeners_.back().name_ = name;
      refs.push_back(listeners_.back());
    }
    EXPECT_CALL(lm_, listeners()).WillOnce(Return(refs));
  }

  Upstream::MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Init::MockManager init_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::IsolatedStoreImpl store_;
  MockListenerManager lm_;
  Http::MockAsyncClientRequest request_;
  std::unique_ptr<LdsApi> lds_;
  Event::MockTimer* interval_timer_{new Event::MockTimer(&dispatcher_)};
  Http::AsyncClient::Callbacks* callbacks_{};

private:
  std::list<NiceMock<MockListener>> listeners_;
};

TEST_F(LdsApiTest, Basic) {
  InSequence s;

  setup();

  std::string response1_json = R"EOF(
  {
    "listeners": [
    {
      "name": "listener1"
    },
    {
      "name": "listener2"
    }
    ]
  }
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response1_json));

  makeListenersAndExpectCall({});
  expectAdd("listener1");
  expectAdd("listener2");
  EXPECT_CALL(init_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  expectRequest();
  interval_timer_->callback_();

  std::string response2_json = R"EOF(
  {
    "listeners": [
    {
      "name": "listener1"
    },
    {
      "name": "listener3"
    }
    ]
  }
  )EOF";

  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response2_json));

  makeListenersAndExpectCall({"listener1", "listener2"});
  expectAdd("listener1");
  expectAdd("listener3");
  EXPECT_CALL(lm_, removeListener("listener2"));
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ(2UL, store_.counter("listener_manager.lds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("listener_manager.lds.update_success").value());
}

TEST_F(LdsApiTest, Failure) {
  InSequence s;

  setup();

  std::string response_json = R"EOF(
  {
    "listeners" : {}
  }
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response_json));

  EXPECT_CALL(init_.initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  expectRequest();
  interval_timer_->callback_();

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ(2UL, store_.counter("listener_manager.lds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("listener_manager.lds.update_failure").value());
}

} // namespace Server
} // namespace Envoy
