#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common/filesystem/filesystem_impl.h"
#include "common/http/message_impl.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/upstream/sds.h"

#include "test/mocks/local_info/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::SaveArg;
using testing::WithArg;

namespace Upstream {

class SdsTest : public testing::Test {
protected:
  SdsTest() : sds_config_{"sds", std::chrono::milliseconds(30000)}, request_(&cm_.async_client_) {
    std::string raw_config = R"EOF(
    {
      "name": "name",
      "connect_timeout_ms": 250,
      "type": "sds",
      "lb_type": "round_robin",
      "service_name": "fare"
    }
    )EOF";

    Json::ObjectPtr config = Json::Factory::loadFromString(raw_config);

    timer_ = new Event::MockTimer(&dispatcher_);
    local_info_.zone_name_ = "us-east-1a";
    cluster_.reset(new SdsClusterImpl(*config, runtime_, stats_, ssl_context_manager_, sds_config_,
                                      local_info_, cm_, dispatcher_, random_));
    EXPECT_EQ(Cluster::InitializePhase::Secondary, cluster_->initializePhase());
  }

  HostSharedPtr findHost(const std::string& address) {
    for (HostSharedPtr host : cluster_->hosts()) {
      if (host->address()->ip()->addressAsString() == address) {
        return host;
      }
    }

    return nullptr;
  }

  uint64_t numHealthy() {
    uint64_t healthy = 0;
    for (HostSharedPtr host : cluster_->hosts()) {
      if (host->healthy()) {
        healthy++;
      }
    }

    return healthy;
  }

  void setupPoolFailure() {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("sds")).WillOnce(ReturnRef(cm_.async_client_));
    EXPECT_CALL(cm_.async_client_,
                send_(_, _, Optional<std::chrono::milliseconds>(std::chrono::milliseconds(1000))))
        .WillOnce(Invoke([](Http::MessagePtr&, Http::AsyncClient::Callbacks& callbacks,
                            Optional<std::chrono::milliseconds>) -> Http::AsyncClient::Request* {
          callbacks.onSuccess(Http::MessagePtr{new Http::ResponseMessageImpl(
              Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}})});
          return nullptr;
        }));
  }

  void setupRequest() {
    EXPECT_CALL(cm_, httpAsyncClientForCluster("sds")).WillOnce(ReturnRef(cm_.async_client_));
    EXPECT_CALL(cm_.async_client_,
                send_(_, _, Optional<std::chrono::milliseconds>(std::chrono::milliseconds(1000))))
        .WillOnce(DoAll(WithArg<1>(SaveArgAddress(&callbacks_)), Return(&request_)));
  }

  Stats::IsolatedStoreImpl stats_;
  Ssl::MockContextManager ssl_context_manager_;
  SdsConfig sds_config_;
  MockClusterManager cm_;
  Event::MockDispatcher dispatcher_;
  std::unique_ptr<SdsClusterImpl> cluster_;
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
  cluster_->initialize();
  EXPECT_CALL(request_, cancel());
  cluster_.reset();
}

TEST_F(SdsTest, PoolFailure) {
  setupPoolFailure();
  EXPECT_CALL(*timer_, enableTimer(_));
  cluster_->initialize();
}

TEST_F(SdsTest, NoHealthChecker) {
  setupRequest();
  cluster_->initialize();

  EXPECT_CALL(membership_updated_, ready()).Times(3);
  cluster_->addMemberUpdateCb(
      [&](const std::vector<HostSharedPtr>&, const std::vector<HostSharedPtr>&)
          -> void { membership_updated_.ready(); });
  cluster_->setInitializedCb([&]() -> void { membership_updated_.ready(); });

  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(Filesystem::fileReadToEnd(
      TestEnvironment::runfilesPath("test/common/upstream/test_data/sds_response.json"))));

  EXPECT_CALL(*timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(13UL, cluster_->hosts().size());
  EXPECT_EQ(13UL, cluster_->healthyHosts().size());
  EXPECT_EQ(13UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone().size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[0].size());
  EXPECT_EQ(5UL, cluster_->healthyHostsPerZone()[1].size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[2].size());

  // Hosts in SDS and static clusters should have empty hostname
  EXPECT_EQ("", cluster_->hosts()[0]->hostname());

  HostSharedPtr canary_host = findHost("10.0.16.43");
  EXPECT_TRUE(canary_host->canary());
  EXPECT_EQ("us-east-1d", canary_host->zone());
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
  EXPECT_CALL(*timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(13UL, cluster_->hosts().size());
  EXPECT_EQ(canary_host, findHost("10.0.16.43"));
  EXPECT_TRUE(canary_host->canary());
  EXPECT_EQ("us-east-1d", canary_host->zone());
  EXPECT_EQ(50U, canary_host->weight());
  EXPECT_EQ(50UL, cluster_->info()->stats().max_host_weight_.value());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone().size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[0].size());
  EXPECT_EQ(5UL, cluster_->healthyHostsPerZone()[1].size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[2].size());

  // Now test the failure case, our cluster size should not change.
  setupRequest();
  timer_->callback_();

  EXPECT_CALL(*timer_, enableTimer(_));
  callbacks_->onFailure(Http::AsyncClient::FailureReason::Reset);
  EXPECT_EQ(13UL, cluster_->hosts().size());
  EXPECT_EQ(50U, canary_host->weight());
  EXPECT_EQ(50UL, cluster_->info()->stats().max_host_weight_.value());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone().size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[0].size());
  EXPECT_EQ(5UL, cluster_->healthyHostsPerZone()[1].size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[2].size());

  // 503 response.
  setupRequest();
  timer_->callback_();

  EXPECT_CALL(*timer_, enableTimer(_));
  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "503"}}}));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(13UL, cluster_->hosts().size());
  EXPECT_EQ(50U, canary_host->weight());
  EXPECT_EQ(50UL, cluster_->info()->stats().max_host_weight_.value());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone().size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[0].size());
  EXPECT_EQ(5UL, cluster_->healthyHostsPerZone()[1].size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[2].size());
}

TEST_F(SdsTest, HealthChecker) {
  MockHealthChecker* health_checker = new MockHealthChecker();
  EXPECT_CALL(*health_checker, start());
  EXPECT_CALL(*health_checker, addHostCheckCompleteCb(_)).Times(2);
  cluster_->setHealthChecker(HealthCheckerPtr{health_checker});
  cluster_->setInitializedCb([&]() -> void { membership_updated_.ready(); });

  setupRequest();
  cluster_->initialize();

  // Load in all of the hosts the first time, this will setup first pass health checking. We expect
  // all the hosts to load in unhealthy.
  Http::MessagePtr message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(Filesystem::fileReadToEnd(
      TestEnvironment::runfilesPath("test/common/upstream/test_data/sds_response.json"))));

  EXPECT_CALL(*timer_, enableTimer(_));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(13UL, cluster_->hosts().size());
  EXPECT_EQ(0UL, cluster_->healthyHosts().size());
  EXPECT_EQ(0UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(0UL, numHealthy());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone().size());
  EXPECT_EQ(3UL, cluster_->hostsPerZone().size());
  EXPECT_EQ(0UL, cluster_->healthyHostsPerZone()[0].size());
  EXPECT_EQ(0UL, cluster_->healthyHostsPerZone()[1].size());
  EXPECT_EQ(0UL, cluster_->healthyHostsPerZone()[2].size());

  // Now run through and make all the hosts healthy except for the first one.
  for (size_t i = 1; i < cluster_->hosts().size(); i++) {
    cluster_->hosts()[i]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
    health_checker->runCallbacks(cluster_->hosts()[i], true);
  }

  EXPECT_EQ(12UL, cluster_->healthyHosts().size());
  EXPECT_EQ(12UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone().size());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone()[0].size());
  EXPECT_EQ(5UL, cluster_->healthyHostsPerZone()[1].size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[2].size());

  // Do the last one now which should fire the initialized event.
  EXPECT_CALL(membership_updated_, ready());
  cluster_->hosts()[0]->healthFlagClear(Host::HealthFlag::FAILED_ACTIVE_HC);
  health_checker->runCallbacks(cluster_->hosts()[0], true);
  EXPECT_EQ(13UL, cluster_->healthyHosts().size());
  EXPECT_EQ(13UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone().size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[0].size());
  EXPECT_EQ(5UL, cluster_->healthyHostsPerZone()[1].size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[2].size());

  // Now we will remove some hosts, but since they are all healthy, they shouldn't actually be gone.
  setupRequest();
  timer_->callback_();

  EXPECT_CALL(*timer_, enableTimer(_));
  message.reset(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  message->body().reset(new Buffer::OwnedImpl(Filesystem::fileReadToEnd(
      TestEnvironment::runfilesPath("test/common/upstream/test_data/sds_response_2.json"))));
  callbacks_->onSuccess(std::move(message));
  EXPECT_EQ(14UL, cluster_->hosts().size());
  EXPECT_EQ(13UL, cluster_->healthyHosts().size());
  EXPECT_EQ(13UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(13UL, numHealthy());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone().size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[0].size());
  EXPECT_EQ(5UL, cluster_->healthyHostsPerZone()[1].size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[2].size());

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
  EXPECT_EQ(13UL, cluster_->hosts().size());
  EXPECT_EQ(12UL, cluster_->healthyHosts().size());
  EXPECT_EQ(12UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(12UL, numHealthy());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone().size());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone()[0].size());
  EXPECT_EQ(5UL, cluster_->healthyHostsPerZone()[1].size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[2].size());

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
  EXPECT_EQ(13UL, cluster_->hosts().size());
  EXPECT_EQ(12UL, cluster_->healthyHosts().size());
  EXPECT_EQ(12UL, cluster_->info()->stats().membership_healthy_.value());
  EXPECT_EQ(12UL, numHealthy());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone().size());
  EXPECT_EQ(3UL, cluster_->healthyHostsPerZone()[0].size());
  EXPECT_EQ(5UL, cluster_->healthyHostsPerZone()[1].size());
  EXPECT_EQ(4UL, cluster_->healthyHostsPerZone()[2].size());
}

TEST_F(SdsTest, Failure) {
  setupRequest();
  cluster_->initialize();

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
  cluster_->initialize();

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

} // Upstream
