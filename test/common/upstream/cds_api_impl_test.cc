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
using testing::AnyNumber;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;
using testing::Throw;

namespace Envoy {
namespace Upstream {
namespace {

MATCHER_P(WithName, expectedName, "") { return arg.name() == expectedName; }

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
    cluster_map_.emplace("foo_cluster", mock_cluster_);
    EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(cluster_map_));
    EXPECT_CALL(mock_cluster_, info()).Times(AnyNumber());
    EXPECT_CALL(*mock_cluster_.info_, addedViaApi());
    EXPECT_CALL(mock_cluster_, info()).Times(AnyNumber());
    EXPECT_CALL(*mock_cluster_.info_, type());
    cds_ = CdsApiImpl::create(cds_config, cm_, dispatcher_, random_, local_info_, store_, *api_);
    resetCdsInitializedCb();

    expectRequest();
    cds_->initialize();
  }

  void resetCdsInitializedCb() {
    cds_->setInitializedCb([this]() -> void {
      initialized_.ready();
      cm_.finishClusterWarming();
    });
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

  static Http::MessagePtr parseResponseMessageFromYaml(const std::string& yaml) {
    Http::MessagePtr message(new Http::ResponseMessageImpl(
        Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
    const auto json = Json::Factory::loadFromYamlString(yaml);
    ASSERT(json->isObject());
    message->body() = std::make_unique<Buffer::OwnedImpl>(json->asJsonString());
    return message;
  }

  class MockWarmingClusterManager : public MockClusterManager {
  public:
    explicit MockWarmingClusterManager(TimeSource& time_source) : MockClusterManager(time_source) {}

    MockWarmingClusterManager() {}

    void expectAdd(const std::string& cluster_name, const std::string& version = std::string("")) {
      EXPECT_CALL(*this, addOrUpdateCluster(WithName(cluster_name), version, _))
          .WillOnce(Return(true));
    }

    void expectAddToThrow(const std::string& cluster_name, const std::string& exception_msg) {
      EXPECT_CALL(*this, addOrUpdateCluster(WithName(cluster_name), _, _))
          .WillOnce(Throw(EnvoyException(exception_msg)));
    }

    void expectAddWithWarming(const std::string& cluster_name, const std::string& version,
                              bool immediately_warm_up = false) {
      EXPECT_CALL(*this, addOrUpdateCluster(_, version, _))
          .WillOnce(Invoke([this, cluster_name,
                            immediately_warm_up](const envoy::api::v2::Cluster& cluster,
                                                 const std::string&, auto warming_cb) -> bool {
            EXPECT_EQ(cluster_name, cluster.name());
            EXPECT_EQ(warming_cbs_.cend(), warming_cbs_.find(cluster.name()));
            warming_cbs_[cluster.name()] = warming_cb;
            warming_cb(cluster.name(), ClusterManager::ClusterWarmingState::Starting);
            if (immediately_warm_up) {
              warming_cbs_.erase(cluster.name());
              warming_cb(cluster.name(), ClusterManager::ClusterWarmingState::Finished);
            }
            return true;
          }));
    }

    void expectWarmingClusterCount(int times = 1) {
      EXPECT_CALL(*this, warmingClusterCount()).Times(times).WillRepeatedly(Invoke([this]() {
        return warming_cbs_.size();
      }));
    }

    void finishClusterWarming() {
      for (const auto& cluster : clusters_to_warm_up_) {
        EXPECT_NE(warming_cbs_.cend(), warming_cbs_.find(cluster));
        auto callback = warming_cbs_[cluster];
        warming_cbs_.erase(cluster);
        callback(cluster, ClusterManager::ClusterWarmingState::Finished);
      }
      clusters_to_warm_up_.clear();
    }

    void clustersToWarmUp(const std::vector<std::string>&& clusters) {
      clusters_to_warm_up_ = clusters;
    }

  private:
    std::map<std::string, ClusterManager::ClusterWarmingCallback> warming_cbs_;
    std::vector<std::string> clusters_to_warm_up_;
  };

  NiceMock<MockWarmingClusterManager> cm_;
  Upstream::ClusterManager::ClusterInfoMap cluster_map_;
  Upstream::MockClusterMockPrioritySet mock_cluster_;
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

  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(cluster_map_));
  EXPECT_CALL(initialized_, ready());
  EXPECT_THROW(dynamic_cast<CdsApiImpl*>(cds_.get())->onConfigUpdate(clusters, ""), EnvoyException);
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

  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(cluster_map_));
  EXPECT_CALL(initialized_, ready());
  EXPECT_THROW_WITH_MESSAGE(dynamic_cast<CdsApiImpl*>(cds_.get())->onConfigUpdate(clusters, ""),
                            EnvoyException,
                            "Error adding/updating cluster(s) duplicate_cluster: duplicate cluster "
                            "duplicate_cluster found");
  EXPECT_CALL(request_, cancel());
}

TEST_F(CdsApiImplTest, EmptyConfigUpdate) {
  InSequence s;

  setup();

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(request_, cancel());

  Protobuf::RepeatedPtrField<envoy::api::v2::Cluster> clusters;
  dynamic_cast<CdsApiImpl*>(cds_.get())->onConfigUpdate(clusters, "");
}

TEST_F(CdsApiImplTest, ConfigUpdateWith2ValidClusters) {
  {
    InSequence s;
    setup();
  }

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(request_, cancel());

  Protobuf::RepeatedPtrField<envoy::api::v2::Cluster> clusters;
  auto* cluster_1 = clusters.Add();
  cluster_1->set_name("cluster_1");
  cm_.expectAdd("cluster_1");

  auto* cluster_2 = clusters.Add();
  cluster_2->set_name("cluster_2");
  cm_.expectAdd("cluster_2");

  dynamic_cast<CdsApiImpl*>(cds_.get())->onConfigUpdate(clusters, "");
}

TEST_F(CdsApiImplTest, ConfigUpdateAddsSecondClusterEvenIfFirstThrows) {
  {
    InSequence s;
    setup();
  }

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(request_, cancel());

  Protobuf::RepeatedPtrField<envoy::api::v2::Cluster> clusters;
  auto* cluster_1 = clusters.Add();
  cluster_1->set_name("cluster_1");
  cm_.expectAddToThrow("cluster_1", "An exception");

  auto* cluster_2 = clusters.Add();
  cluster_2->set_name("cluster_2");
  cm_.expectAdd("cluster_2");

  auto* cluster_3 = clusters.Add();
  cluster_3->set_name("cluster_3");
  cm_.expectAddToThrow("cluster_3", "Another exception");

  EXPECT_THROW_WITH_MESSAGE(
      dynamic_cast<CdsApiImpl*>(cds_.get())->onConfigUpdate(clusters, ""), EnvoyException,
      "Error adding/updating cluster(s) cluster_1: An exception, cluster_3: Another exception");
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

  const std::string response1_yaml = R"EOF(
version_info: '0'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster2
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(ClusterManager::ClusterInfoMap{}));
  cm_.expectAdd("cluster1", "0");
  cm_.expectAdd("cluster2", "0");
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  EXPECT_EQ("", cds_->versionInfo());
  EXPECT_EQ(0UL, store_.gauge("cluster_manager.cds.version").value());
  callbacks_->onSuccess(parseResponseMessageFromYaml(response1_yaml));
  EXPECT_EQ("0", cds_->versionInfo());
  EXPECT_EQ(7148434200721666028U, store_.gauge("cluster_manager.cds.version").value());

  expectRequest();
  interval_timer_->callback_();

  const std::string response2_yaml = R"EOF(
version_info: '1'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster3
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";

  EXPECT_CALL(cm_, clusters()).WillOnce(Return(makeClusterMap({"cluster1", "cluster2"})));
  cm_.expectAdd("cluster1", "1");
  cm_.expectAdd("cluster3", "1");
  EXPECT_CALL(cm_, removeCluster("cluster2"));
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(parseResponseMessageFromYaml(response2_yaml));

  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_attempt").value());
  EXPECT_EQ(2UL, store_.counter("cluster_manager.cds.update_success").value());
  EXPECT_EQ("1", cds_->versionInfo());
  EXPECT_EQ(13237225503670494420U, store_.gauge("cluster_manager.cds.version").value());
}

TEST_F(CdsApiImplTest, CdsPauseOnWarming) {
  interval_timer_ = new Event::MockTimer(&dispatcher_);
  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(ClusterManager::ClusterInfoMap{}));
  InSequence s;

  setup();

  const std::string response1_yaml = R"EOF(
version_info: '0'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster2
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";

  // Two clusters updated, both warmed up.
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().ClusterLoadAssignment)).Times(1);
  cm_.expectAddWithWarming("cluster1", "0");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().Cluster)).Times(1);
  cm_.expectAddWithWarming("cluster2", "0");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(initialized_, ready());
  cm_.expectWarmingClusterCount(2);
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().Cluster)).Times(1);
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().ClusterLoadAssignment)).Times(1);
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  cm_.clustersToWarmUp({"cluster1", "cluster2"});
  callbacks_->onSuccess(parseResponseMessageFromYaml(response1_yaml));

  expectRequest();
  interval_timer_->callback_();

  // Two clusters updated, only one warmed up.
  const std::string response2_yaml = R"EOF(
version_info: '1'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster3
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";

  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().ClusterLoadAssignment)).Times(1);
  cm_.expectAddWithWarming("cluster1", "1");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().Cluster)).Times(1);
  cm_.expectAddWithWarming("cluster3", "1");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(initialized_, ready());
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().ClusterLoadAssignment)).Times(1);
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  resetCdsInitializedCb();
  cm_.clustersToWarmUp({"cluster1"});
  callbacks_->onSuccess(parseResponseMessageFromYaml(response2_yaml));

  expectRequest();
  interval_timer_->callback_();

  // One cluster updated and warmed up. Also finish warming up of the previously added cluster3.
  const std::string response3_yaml = R"EOF(
version_info: '2'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster4
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";

  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().ClusterLoadAssignment)).Times(1);
  cm_.expectAddWithWarming("cluster4", "2");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(initialized_, ready());
  cm_.expectWarmingClusterCount(2);
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().Cluster)).Times(1);
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().ClusterLoadAssignment)).Times(1);
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  resetCdsInitializedCb();
  cm_.clustersToWarmUp({"cluster4", "cluster3"});
  callbacks_->onSuccess(parseResponseMessageFromYaml(response3_yaml));

  expectRequest();
  interval_timer_->callback_();

  const std::string response4_yaml = R"EOF(
version_info: '3'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster5
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster6
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";

  // Two clusters updated, first one warmed up before processing of the second one starts.
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().ClusterLoadAssignment)).Times(1);
  cm_.expectAddWithWarming("cluster5", "3", true);
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().Cluster)).Times(1);
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().Cluster)).Times(1);
  cm_.expectAddWithWarming("cluster6", "3");
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, pause(Config::TypeUrl::get().Cluster)).Times(1);
  EXPECT_CALL(initialized_, ready());
  cm_.expectWarmingClusterCount();
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().Cluster)).Times(1);
  EXPECT_CALL(cm_.ads_mux_, resume(Config::TypeUrl::get().ClusterLoadAssignment)).Times(1);
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  resetCdsInitializedCb();
  cm_.clustersToWarmUp({"cluster6"});
  callbacks_->onSuccess(parseResponseMessageFromYaml(response4_yaml));
}

TEST_F(CdsApiImplTest, Failure) {
  interval_timer_ = new Event::MockTimer(&dispatcher_);
  InSequence s;

  setup();

  const std::string response_yaml = R"EOF(
version_info: '0'
resources:
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
- "@type": type.googleapis.com/envoy.api.v2.Cluster
  name: cluster1
  type: EDS
  eds_cluster_config:
    eds_config:
      path: eds path
)EOF";

  EXPECT_CALL(cm_, clusters()).WillRepeatedly(Return(cluster_map_));
  EXPECT_CALL(initialized_, ready());
  EXPECT_CALL(*interval_timer_, enableTimer(_));
  callbacks_->onSuccess(parseResponseMessageFromYaml(response_yaml));

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

} // namespace
} // namespace Upstream
} // namespace Envoy
