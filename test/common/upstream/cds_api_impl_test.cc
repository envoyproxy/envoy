#include <chrono>
#include <memory>
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

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Upstream {

class CdsApiImplTest : public testing::Test {
protected:
  CdsApiImplTest() : request_(&cm_.async_client_), api_(Api::createApiForTest(store_)) {}

  void setup() {
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
    cds_config.mutable_api_config_source()->set_api_type(
        envoy::api::v2::core::ApiConfigSource::REST);
    Upstream::ClusterManager::ClusterInfoMap cluster_map;
    Upstream::MockCluster cluster;
    cluster_map.emplace("foo_cluster", cluster);
    EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
    EXPECT_CALL(cluster, info());
    EXPECT_CALL(*cluster.info_, addedViaApi());
    EXPECT_CALL(cluster, info());
    EXPECT_CALL(*cluster.info_, type());
    cds_ = CdsApiImpl::create(cds_config, cm_, dispatcher_, random_, local_info_, store_, *api_);
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
        .WillOnce(
            Invoke([&](Http::MessagePtr& request, Http::AsyncClient::Callbacks& callbacks,
                       const Http::AsyncClient::RequestOptions&) -> Http::AsyncClient::Request* {
              EXPECT_EQ(
                  (Http::TestHeaderMapImpl{
                      {":method", "POST"},
                      {":path", "/v2/discovery:clusters"},
                      {":authority", "foo_cluster"},
                      {"content-type", "application/json"},
                      {"content-length",
                       request->body() ? fmt::format_int(request->body()->length()).str() : "0"}}),
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
  Api::ApiPtr api_;
};

// Negative test for protoc-gen-validate constraints.
TEST_F(CdsApiImplTest, ValidateFail) {
  InSequence s;

  setup();

  Protobuf::RepeatedPtrField<envoy::api::v2::Cluster> clusters;
  clusters.Add();

  EXPECT_THROW(dynamic_cast<CdsApiImpl*>(cds_.get())->onConfigUpdate(clusters, ""),
               ProtoValidationException);
  EXPECT_CALL(request_, cancel());
}

// Validate onConfigUpdate throws EnvoyException with duplicate clusters.
TEST_F(CdsApiImplTest, ValidateDuplicateClusters) {
  InSequence s;

  setup();

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
      CdsApiImpl::create(cds_config, cm_, dispatcher_, random_, local_info_, store_, *api_),
      EnvoyException);
}

TEST_F(CdsApiImplTest, Basic) {
  interval_timer_ = new Event::MockTimer(&dispatcher_);
  InSequence s;

  setup();

  const std::string response1_json = R"EOF(
{
  "version_info": "0",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.api.v2.Cluster",
      "name": "cluster1",
      "type": "EDS",
      "eds_cluster_config": { "eds_config": { "path": "eds path" } }
    },
    {
      "@type": "type.googleapis.com/envoy.api.v2.Cluster",
      "name": "cluster2",
      "type": "EDS",
      "eds_cluster_config": { "eds_config": { "path": "eds path" } }
    },
  ]
}
)EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body() = std::make_unique<Buffer::OwnedImpl>(response1_json);

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  expectAdd("cluster1", "0");
  expectAdd("cluster2", "0");
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_EQ("", cds_->versionInfo());
  EXPECT_EQ(0UL, store_.gauge("cluster_manager.cds.version").value());
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ("0", cds_->versionInfo());
  EXPECT_EQ(7148434200721666028U, store_.gauge("cluster_manager.cds.version").value());

  expectRequest();
  interval_timer_->callback_();

  const std::string response2_json = R"EOF(
{
  "version_info": "1",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.api.v2.Cluster",
      "name": "cluster1",
      "type": "EDS",
      "eds_cluster_config": { "eds_config": { "path": "eds path" } }
    },
    {
      "@type": "type.googleapis.com/envoy.api.v2.Cluster",
      "name": "cluster3",
      "type": "EDS",
      "eds_cluster_config": { "eds_config": { "path": "eds path" } }
    },
  ]
}
)EOF";

  message = std::make_unique<Http::ResponseMessageImpl>(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}});
  message->body() = std::make_unique<Buffer::OwnedImpl>(response2_json);

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterMap({"cluster1", "cluster2"})));
  expectAdd("cluster1", "1");
  expectAdd("cluster3", "1");
  EXPECT_CALL(cm_, removeCluster("cluster2"));
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_success").value());
  EXPECT_EQ("1", cds_->versionInfo());
  EXPECT_EQ(13237225503670494420U, store_.gauge("cluster_manager.cds.version").value());
}

TEST_F(CdsApiImplTest, Failure) {
  interval_timer_ = new Event::MockTimer(&dispatcher_);
  InSequence s;

  setup();

  const std::string response_json = R"EOF(
{
  "version_info": "0",
  "resources": [
    {
      "@type": "type.googleapis.com/envoy.api.v2.Cluster",
      "name": "cluster1",
      "type": "EDS",
      "eds_cluster_config": { "eds_config": { "path": "eds path" } }
    },
    {
      "@type": "type.googleapis.com/envoy.api.v2.Cluster",
      "name": "cluster1",
      "type": "EDS",
      "eds_cluster_config": { "eds_config": { "path": "eds path" } }
    },
  ]
}
)EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body() = std::make_unique<Buffer::OwnedImpl>(response_json);

  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  expectRequest();
  interval_timer_->callback_();

  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);

  EXPECT_EQ("", cds_->versionInfo());
  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_attempt").value());
  EXPECT_EQ(1UL, store_.counter("cluster_manager.cds.update_failure").value());
  // Validate that the schema error increments update_rejected stat.
  EXPECT_EQ(1UL, store_.counter("cluster_manager.cds.update_rejected").value());
  EXPECT_EQ(0UL, store_.gauge("cluster_manager.cds.version").value());
}

} // namespace Upstream
} // namespace Envoy
