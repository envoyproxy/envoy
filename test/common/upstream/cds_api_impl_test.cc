#include <chrono>
#include <string>
#include <vector>

#include "common/config/utility.h"
#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/upstream/cds_api_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Upstream {

class CdsApiImplTest : public testing::Test {
public:
  CdsApiImplTest() : request_(&cm_.async_client_) {}

  void setup(bool v2_rest = false) {
    v2_rest_ = v2_rest;
    const std::string config_json = R"EOF(
    {
      "cluster": {
        "name": "foo_cluster"
      }
    }
    )EOF";

    Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);
    envoy::api::v2::core::ConfigSource cds_config;
    Config::Utility::translateCdsConfig(*config, cds_config);
    if (v2_rest) {
      cds_config.mutable_api_config_source()->set_api_type(
          envoy::api::v2::core::ApiConfigSource::REST);
    }
    Upstream::ClusterManager::ClusterInfoMap cluster_map;
    Upstream::MockCluster cluster;
    cluster_map.emplace("foo_cluster", cluster);
    EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
    EXPECT_CALL(cluster, info());
    EXPECT_CALL(*cluster.info_, addedViaApi());
    EXPECT_CALL(cluster, info());
    EXPECT_CALL(*cluster.info_, type());
    cds_ =
        CdsApiImpl::create(cds_config, eds_config_, cm_, dispatcher_, random_, local_info_, store_);
    cds_->setInitializedCb([this]() -> void { initialized_.ready(); });

    expectRequest();
    cds_->initialize();
  }

  void expectAdd(const std::string& cluster_name, const std::string& version) {
    EXPECT_CALL(cm_, addOrUpdateCluster(_, version))
        .WillOnce(Invoke(
            [cluster_name](const envoy::api::v2::Cluster& cluster, const std::string&) -> bool {
              EXPECT_EQ(cluster_name, cluster.name());
              return true;
            }));
  }

  void expectRequest() {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("foo_cluster"));
    EXPECT_CALL(cm_.async_client_, send_(_, _, _))
        .WillOnce(Invoke(
            [&](Http::MessagePtr& request, Http::AsyncClient::Callbacks& callbacks,
                const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
              EXPECT_EQ((Http::TestHeaderMapImpl{
                            {":method", v2_rest_ ? "POST" : "GET"},
                            {":path", v2_rest_ ? "/v2/discovery:clusters"
                                               : "/v1/clusters/cluster_name/node_name"},
                            {":authority", "foo_cluster"}}),
                        request->headers());
              callbacks_ = &callbacks;
              return &request_;
            }));
  }

  ClusterManager::ClusterInfoMap makeClusterMap(std::vector<std::string> clusters) {
    ClusterManager::ClusterInfoMap map;
    for (auto cluster : clusters) {
      map.emplace(cluster, cm_.thread_local_cluster_.cluster_);
    }
    return map;
  }

  bool v2_rest_{};
  NiceMock<MockClusterManager> cm_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Stats::IsolatedStoreImpl store_;
  Http::MockAsyncClientRequest request_;
  CdsApiPtr cds_;
  Event::MockTimer* interval_timer_;
  Http::AsyncClient::Callbacks* callbacks_{};
  ReadyWatcher initialized_;
  absl::optional<envoy::api::v2::core::ConfigSource> eds_config_;
};

// Negative test for protoc-gen-validate constraints.
TEST_F(CdsApiImplTest, ValidateFail) {
  InSequence s;

  setup(true);

  Protobuf::RepeatedPtrField<envoy::api::v2::Cluster> clusters;
  clusters.Add();

  EXPECT_THROW(dynamic_cast<CdsApiImpl*>(cds_.get())->onConfigUpdate(clusters, ""),
               ProtoValidationException);
  EXPECT_CALL(request_, cancel());
}

// Validate onConfigUpadte throws EnvoyException with duplicate clusters.
TEST_F(CdsApiImplTest, ValidateDuplicateClusters) {
  InSequence s;

  setup(true);

  Protobuf::RepeatedPtrField<envoy::api::v2::Cluster> clusters;
  auto* cluster_1 = clusters.Add();
  cluster_1->set_name("duplicate_cluster");

  auto* cluster_2 = clusters.Add();
  cluster_2->set_name("duplicate_cluster");

  EXPECT_THROW_WITH_MESSAGE(dynamic_cast<CdsApiImpl*>(cds_.get())->onConfigUpdate(clusters, ""),
                            EnvoyException, "duplicate cluster duplicate_cluster found");
  EXPECT_CALL(request_, cancel());
}

TEST_F(CdsApiImplTest, InvalidOptions) {
  const std::string config_json = R"EOF(
  {
    "cluster": {
      "name": "foo_cluster"
    }
  }
  )EOF";

  Json::ObjectSharedPtr config = Json::Factory::loadFromString(config_json);
  local_info_.node_.set_cluster("");
  local_info_.node_.set_id("");
  envoy::api::v2::core::ConfigSource cds_config;
  Config::Utility::translateCdsConfig(*config, cds_config);
  EXPECT_THROW(
      CdsApiImpl::create(cds_config, eds_config_, cm_, dispatcher_, random_, local_info_, store_),
      EnvoyException);
}

TEST_F(CdsApiImplTest, Basic) {
  interval_timer_ = new Event::MockTimer(&dispatcher_);
  InSequence s;

  setup();

  const std::string response1_json = fmt::sprintf(
      "{%s}",
      clustersJson({defaultStaticClusterJson("cluster1"), defaultStaticClusterJson("cluster2")}));

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response1_json));

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  expectAdd("cluster1", "hash_3845eb3523492899");
  expectAdd("cluster2", "hash_3845eb3523492899");
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_EQ("", cds_->versionInfo());
  EXPECT_EQ(0UL, store_.gauge("cluster_manager.cds.version").value());
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(Config::Utility::computeHashedVersion(response1_json).first, cds_->versionInfo());
  EXPECT_EQ(4054905652974790809U, store_.gauge("cluster_manager.cds.version").value());

  expectRequest();
  interval_timer_->callback_();

  const std::string response2_json = fmt::sprintf(
      "{%s}",
      clustersJson({defaultStaticClusterJson("cluster1"), defaultStaticClusterJson("cluster3")}));

  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response2_json));

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterMap({"cluster1", "cluster2"})));
  expectAdd("cluster1", "hash_19fd657104a2cd34");
  expectAdd("cluster3", "hash_19fd657104a2cd34");
  EXPECT_CALL(cm_, removeCluster("cluster2"));
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_success").value());
  EXPECT_EQ(Config::Utility::computeHashedVersion(response2_json).first, cds_->versionInfo());
  EXPECT_EQ(1872764556139482420U, store_.gauge("cluster_manager.cds.version").value());
}

TEST_F(CdsApiImplTest, Failure) {
  interval_timer_ = new Event::MockTimer(&dispatcher_);
  InSequence s;

  setup();

  const std::string response_json = R"EOF(
  {
    "clusters" : {}
  }
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response_json));

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  expectRequest();
  interval_timer_->callback_();

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ("", cds_->versionInfo());
  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_failure").value());
  EXPECT_EQ(0UL, store_.gauge("cluster_manager.cds.version").value());
}

TEST_F(CdsApiImplTest, FailureArray) {
  interval_timer_ = new Event::MockTimer(&dispatcher_);
  InSequence s;

  setup();

  const std::string response_json = R"EOF(
  []
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(response_json));

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ("", cds_->versionInfo());
  EXPECT_EQ(1UL, store_.counter("cluster_manager.cds.update_attempt").value());
  EXPECT_EQ(1UL, store_.counter("cluster_manager.cds.update_failure").value());
  EXPECT_EQ(0UL, store_.gauge("cluster_manager.cds.version").value());
}

} // namespace Upstream
} // namespace Envoy
