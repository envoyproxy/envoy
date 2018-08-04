#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/api/v2/core/base.pb.h"

#include "common/config/utility.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/http/message_impl.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/upstream/eds.h"

#include "server/transport_socket_config_impl.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::DoAll;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::SaveArg;
using testing::WithArg;
using testing::_;

namespace Envoy {
namespace Upstream {

class SdsTest : public testing::Test {
protected:
  SdsTest() : request_(&cm_.async_client_) { resetCluster(false); }

  void resetCluster(bool set_request_timeout) {
    std::string raw_config = R"EOF(
    {
      "name": "name",
      "connect_timeout_ms": 250,
      "type": "sds",
      "lb_type": "round_robin",
      "service_name": "fare"
    }
    )EOF";

    timer_ = new Event::MockTimer(&dispatcher_);
    local_info_.node_.mutable_locality()->set_zone("us-east-1a");
    envoy::api::v2::core::ConfigSource eds_config;
    eds_config.mutable_api_config_source()->add_cluster_names("sds");
    eds_config.mutable_api_config_source()->mutable_refresh_delay()->set_seconds(1);
    if (set_request_timeout) {
      eds_config.mutable_api_config_source()->mutable_request_timeout()->CopyFrom(
          Protobuf::util::TimeUtil::MillisecondsToDuration(5000));
    }
    sds_cluster_ = parseSdsClusterFromJson(raw_config, eds_config);
    Upstream::ClusterManager::ClusterInfoMap cluster_map;
    Upstream::MockCluster cluster;
    cluster_map.emplace("sds", cluster);
    EXPECT_CALL(cm_, clusters()).WillOnce(Return(cluster_map));
    EXPECT_CALL(cluster, info()).Times(2);
    EXPECT_CALL(*cluster.info_, addedViaApi());
    Envoy::Stats::ScopePtr scope = stats_.createScope(
        fmt::format("cluster.{}.", sds_cluster_.alt_stat_name().empty()
                                       ? sds_cluster_.name()
                                       : std::string(sds_cluster_.alt_stat_name())));
    Envoy::Server::Configuration::TransportSocketFactoryContextImpl factory_context(
        ssl_context_manager_, *scope, cm_, local_info_, dispatcher_, random_, stats_);
    cluster_.reset(
        new EdsClusterImpl(sds_cluster_, runtime_, factory_context, std::move(scope), false));
    EXPECT_EQ(Cluster::InitializePhase::Secondary, cluster_->initializePhase());
  }

  HostSharedPtr findHost(const std::string& address) {
    for (HostSharedPtr host : cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()) {
      if (host->address()->ip()->addressAsString() == address) {
        return host;
      }
    }

    return nullptr;
  }

  uint64_t numHealthy() {
    uint64_t healthy = 0;
    for (const HostSharedPtr& host : cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()) {
      if (host->healthy()) {
        healthy++;
      }
    }

    return healthy;
  }

  void setupPoolFailure() {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("sds")).WillOnce(ReturnRef(cm_.async_client_));
    EXPECT_CALL(
        cm_.async_client_,
        send_(_, _, absl::optional<std::chrono::milliseconds>(std::chrono::milliseconds(1000))))
        .WillOnce(
            Invoke([](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                      absl::optional<std::chrono::milliseconds>) -> Http::AsyncClient::Request* {
              callbacks.onSuccess(Http::MessagePtr{new Http::ResponseMessageImpl(
                  Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}})});
              return nullptr;
            }));
  }

  void setupRequest() {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("sds")).WillOnce(ReturnRef(cm_.async_client_));
    EXPECT_CALL(
        cm_.async_client_,
        send_(_, _, absl::optional<std::chrono::milliseconds>(std::chrono::milliseconds(1000))))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callbacks_)), Return(&request_)));
  }

  Stats::IsolatedStoreImpl stats_;
  Ssl::MockContextManager ssl_context_manager_;
  envoy::api::v2::Cluster sds_cluster_;
  NiceMock<MockClusterManager> cm_;
  Event::MockDispatcher dispatcher_;
  std::shared_ptr<EdsClusterImpl> cluster_;
  Event::MockTimer* timer_;
  Http::AsyncClient::Callbacks* callbacks_;
  ReadyWatcher membership_updated_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  Http::MockAsyncClientRequest request_;
  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
};

TEST_F(SdsTest, Shutdown) {
  setupRequest();
  cluster_->initialize([] {});
  EXPECT_CALL(request_, cancel());
  cluster_.reset();
}

TEST_F(SdsTest, PoolFailure) {
  setupPoolFailure();
  EXPECT_CALL(*timer_, enableTimer(_));
  cluster_->initialize([] {});
}

TEST_F(SdsTest, RequestTimeout) {
  resetCluster(true);

  EXPECT_CALL(cm_, httpAsyncClientForCluster("sds")).WillOnce(ReturnRef(cm_.async_client_));
  EXPECT_CALL(
      cm_.async_client_,
      send_(_, _, absl::optional<std::chrono::milliseconds>(std::chrono::milliseconds(5000))))
      .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callbacks_)), Return(&request_)));

  cluster_->initialize([] {});
  EXPECT_CALL(request_, cancel());
  cluster_.reset();
}

TEST_F(SdsTest, NoHealthChecker) {
  InSequence s;
  setupRequest();

  cluster_->prioritySet().addMemberUpdateCb(
      [&](uint32_t, const HostVector&, const HostVector&) -> void { membership_updated_.ready(); });
  cluster_->initialize([&]() -> void { membership_updated_.ready(); });

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(Filesystem::fileReadToEnd(
      TestEnvironment::runfilesPath("test/common/upstream/test_data/sds_response.json"))));

  EXPECT_CALL(membership_updated_, ready()).Times(2);
  EXPECT_CALL(*timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(13UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(13UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(13UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[0].size());
  EXPECT_EQ(
      5UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[1].size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[2].size());
  EXPECT_EQ(6860994315024339285U, cluster_->info()->stats().version_.value());

  // Hosts in SDS and static clusters should have empty hostname
  EXPECT_EQ("", cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->hostname());

  HostSharedPtr canary_host = findHost("10.0.16.43");
  EXPECT_TRUE(canary_host->canary());
  EXPECT_EQ("us-east-1d", canary_host->locality().zone());
  EXPECT_EQ(40U, canary_host->weight());
  EXPECT_EQ(90UL, cluster_->info()->stats().max_host_weight_.value());

  // Test response with weight change. We should still have the same host.
  setupRequest();
  timer_->callback_();

  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(
      new Buffer::OwnedImpl(Filesystem::fileReadToEnd(TestEnvironment::runfilesPath(
          "test/common/upstream/test_data/sds_response_weight_change.json"))));
  EXPECT_CALL(membership_updated_, ready());
  EXPECT_CALL(*timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(13UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(canary_host, findHost("10.0.16.43"));
  EXPECT_TRUE(canary_host->canary());
  EXPECT_EQ("us-east-1d", canary_host->locality().zone());
  EXPECT_EQ(50U, canary_host->weight());
  EXPECT_EQ(50UL, cluster_->info()->stats().max_host_weight_.value());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[0].size());
  EXPECT_EQ(
      5UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[1].size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[2].size());
  EXPECT_EQ(3250177750903005831U, cluster_->info()->stats().version_.value());

  // Now test the failure case, our cluster size should not change.
  setupRequest();
  timer_->callback_();

  EXPECT_CALL(*timer_, enableTimer(_));
  callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);
  EXPECT_EQ(13UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(50U, canary_host->weight());
  EXPECT_EQ(50UL, cluster_->info()->stats().max_host_weight_.value());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[0].size());
  EXPECT_EQ(
      5UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[1].size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[2].size());

  // 503 response.
  setupRequest();
  timer_->callback_();

  EXPECT_CALL(*timer_, enableTimer(_));
  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}}));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(13UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(50U, canary_host->weight());
  EXPECT_EQ(50UL, cluster_->info()->stats().max_host_weight_.value());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[0].size());
  EXPECT_EQ(
      5UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[1].size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[2].size());
  EXPECT_EQ(3250177750903005831U, cluster_->info()->stats().version_.value());
}

TEST_F(SdsTest, HealthChecker) {
  InSequence s;
  std::shared_ptr<MockHealthChecker> health_checker(new MockHealthChecker());
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  cluster_->setHealthChecker(health_checker);

  setupRequest();
  cluster_->initialize([&]() -> void { membership_updated_.ready(); });

  // Load in all of the hosts the first time, this will setup first pass health checking. We expect
  // all the hosts to load in unhealthy.
  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(Filesystem::fileReadToEnd(
      TestEnvironment::runfilesPath("test/common/upstream/test_data/sds_response.json"))));

  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_));
  EXPECT_CALL(*timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(13UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(0UL, numHealthy());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  EXPECT_EQ(3UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[0].size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[1].size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[2].size());
  EXPECT_EQ(6860994315024339285U, cluster_->info()->stats().version_.value());

  // Now run through and make all the hosts healthy except for the first one. Because we are
  // blocking HC updates, they should all still be unhealthy.
  for (size_t i = 1; i < cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size(); i++) {
    cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[i]->healthFlagClear(
        Host::HealthFlag::FAILED_ACTIVE_HC);
    health_checker->runCallbacks(cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[i],
                                 HealthTransition::Changed);
  }

  EXPECT_EQ(0UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[0].size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[1].size());
  EXPECT_EQ(
      0UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[2].size());

  // Do the last one now which should fire the initialized event. It should also cause a healthy
  // host recalculation and unblock health updates.
  EXPECT_CALL(membership_updated_, ready());
  cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0]->healthFlagClear(
      Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster_->prioritySet().hostSetsPerPriority()[0]->hosts()[0],
                               HealthTransition::Changed);
  EXPECT_EQ(13UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(13UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[0].size());
  EXPECT_EQ(
      5UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[1].size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[2].size());
  EXPECT_EQ(6860994315024339285U, cluster_->info()->stats().version_.value());

  // Now we will remove some hosts, but since they are all healthy, they shouldn't actually be gone.
  setupRequest();
  timer_->callback_();

  EXPECT_CALL(*timer_, enableTimer(_));
  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(Filesystem::fileReadToEnd(
      TestEnvironment::runfilesPath("test/common/upstream/test_data/sds_response_2.json"))));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(14UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(13UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(13UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(13UL, numHealthy());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[0].size());
  EXPECT_EQ(
      5UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[1].size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[2].size());
  EXPECT_EQ(4145707046791707664U, cluster_->info()->stats().version_.value());

  // Now set one of the removed hosts to unhealthy, and return the same query again, this should
  // remove it.
  findHost("10.0.5.0")->healthFlagSet(Host::HealthFlag::FAILED_ACTIVE_HC);
  setupRequest();
  timer_->callback_();
  EXPECT_CALL(*timer_, enableTimer(_));
  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(Filesystem::fileReadToEnd(
      TestEnvironment::runfilesPath("test/common/upstream/test_data/sds_response_2.json"))));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(13UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(12UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(12UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(12UL, numHealthy());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[0].size());
  EXPECT_EQ(
      5UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[1].size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[2].size());
  EXPECT_EQ(4145707046791707664U, cluster_->info()->stats().version_.value());

  // Now add back one of the hosts that was previously missing but we still have and make sure
  // nothing changes.
  setupRequest();
  timer_->callback_();
  EXPECT_CALL(*timer_, enableTimer(_));
  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(Filesystem::fileReadToEnd(
      TestEnvironment::runfilesPath("test/common/upstream/test_data/sds_response_3.json"))));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(13UL, cluster_->prioritySet().hostSetsPerPriority()[0]->hosts().size());
  EXPECT_EQ(12UL, cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHosts().size());
  EXPECT_EQ(12UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(12UL, numHealthy());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get().size());
  EXPECT_EQ(
      3UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[0].size());
  EXPECT_EQ(
      5UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[1].size());
  EXPECT_EQ(
      4UL,
      cluster_->prioritySet().hostSetsPerPriority()[0]->healthyHostsPerLocality().get()[2].size());
  EXPECT_EQ(13400729244851125029U, cluster_->info()->stats().version_.value());
}

TEST_F(SdsTest, Failure) {
  setupRequest();
  cluster_->initialize([] {});

  std::string bad_response_json = R"EOF(
  {
    "hosts" : {}
  }
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(bad_response_json));

  EXPECT_CALL(*timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ(1UL, cluster_->info()->stats().update_failure_.value());
}

TEST_F(SdsTest, FailureArray) {
  setupRequest();
  cluster_->initialize([] {});

  std::string bad_response_json = R"EOF(
  []
  )EOF";

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(bad_response_json));

  EXPECT_CALL(*timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));

  EXPECT_EQ(1UL, cluster_->info()->stats().update_failure_.value());
}

} // namespace Upstream
} // namespace Envoy
