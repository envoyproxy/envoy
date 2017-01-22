#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/upstream/cds_api_impl.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Upstream {

class CdsApiImplTest : public testing::Test {
public:
  CdsApiImplTest() : request_(&cm_.async_client_) {}

  void setup() {
    std::string config_json = R"EOF(
    {
      "cds": {
        "cluster": {
          "name": "foo_cluster"
        }
      }
    }
    )EOF";

    Json::ObjectPtr config = Json::Factory::LoadFromString(config_json);
    cds_ = CdsApiImpl::create(*config, cm_, dispatcher_, random_, local_info_, store_);

    expectRequest();
    cds_->initialize();
  }

  void expectAdd(const std::string& cluster_name) {
    EXPECT_CALL(cm_, addOrUpdatePrimaryCluster(_))
        .WillOnce(Invoke([cluster_name](const Json::Object& config) -> bool {
          EXPECT_EQ(cluster_name, config.getString("name"));
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
                                                 {":path", "/v1/clusters/cluster_name/node_name"},
                                                 {":authority", "foo_cluster"}}),
                        request->headers());
              callbacks_ = &callbacks;
              return &request_;
            }));
  }

  ClusterManager::ClusterInfoMap makeClusterMap(std::vector<std::string> clusters) {
    ClusterManager::ClusterInfoMap map;
    for (auto cluster : clusters) {
      map.emplace(cluster, cm_.cluster_);
    }
    return map;
  }

  MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::IsolatedStoreImpl store_;
  Http::MockAsyncClientRequest request_;
  CdsApiPtr cds_;
  Event::MockTimer* interval_timer_{new Event::MockTimer(&dispatcher_)};
  Http::AsyncClient::Callbacks* callbacks_{};
};

TEST_F(CdsApiImplTest, InvalidOptions) {
  std::string config_json = R"EOF(
  {
    "cds": {
      "cluster": {
        "name": "foo_cluster"
      }
    }
  }
  )EOF";

  Json::ObjectPtr config = Json::Factory::LoadFromString(config_json);
  local_info_.cluster_name_ = "";
  local_info_.node_name_ = "";
  EXPECT_THROW(CdsApiImpl::create(*config, cm_, dispatcher_, random_, local_info_, store_),
               EnvoyException);
}

TEST_F(CdsApiImplTest, Basic) {
  InSequence s;

  setup();

  std::string response1_json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster1"
    },
    {
      "name": "cluster2"
    }
    ]
  }
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body(Buffer::InstancePtr{new Buffer::OwnedImpl(response1_json)});

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  expectAdd("cluster1");
  expectAdd("cluster2");
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  expectRequest();
  interval_timer_->callback_();

  std::string response2_json = R"EOF(
  {
    "clusters": [
    {
      "name": "cluster1"
    },
    {
      "name": "cluster3"
    }
    ]
  }
  )EOF";

  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body(Buffer::InstancePtr{new Buffer::OwnedImpl(response2_json)});

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterMap({"cluster1", "cluster2"})));
  expectAdd("cluster1");
  expectAdd("cluster3");
  EXPECT_CALL(cm_, removePrimaryCluster("cluster2"));
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_success").value());
}

TEST_F(CdsApiImplTest, Failure) {
  InSequence s;

  setup();

  std::string response1_json = R"EOF(
  {
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

  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_failure").value());
}

} // Upstream
