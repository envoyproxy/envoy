#include <memory>
#include <string>
#include <utility>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/load_balancing_policies/quota_aware/v3/quota_aware.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/http/codes.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/config/metadata.h"
#include "source/extensions/load_balancing_policies/quota_aware/config.h"
#include "source/extensions/load_balancing_policies/quota_aware/load_balancer.h"
#include "source/extensions/load_balancing_policies/round_robin/config.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/mocks/upstream/cluster_info.h"
#include "test/mocks/upstream/host_set.h"
#include "test/mocks/upstream/load_balancer_context.h"
#include "test/mocks/upstream/priority_set.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace QuotaAware {
namespace {

using ::envoy::extensions::load_balancing_policies::quota_aware::v3::QuotaAware;
using ::envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;
using ::Envoy::Upstream::HostConstSharedPtr;
using ::Envoy::Upstream::HostSharedPtr;
using ::Envoy::Upstream::MockHostSet;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

class QuotaAwareLoadBalancerTest : public ::testing::Test {
public:
  void SetUp() override {
    ON_CALL(load_balancer_context_, requestStreamInfo()).WillByDefault(Return(&stream_info_));
    ON_CALL(load_balancer_context_, determinePriorityLoad(_, _, _))
        .WillByDefault(
            Invoke([](const Upstream::PrioritySet&, const Upstream::HealthyAndDegradedLoad& original,
                      const Upstream::RetryPriority::PriorityMappingFunc&)
                       -> const Upstream::HealthyAndDegradedLoad& { return original; }));
    ON_CALL(load_balancer_context_, shouldSelectAnotherHost(_)).WillByDefault(Return(false));
    ON_CALL(load_balancer_context_, hostSelectionRetryCount()).WillByDefault(Return(1));
  }

protected:
  QuotaAware makeConfig(bool fail_closed = false) {
    QuotaAware config;
    config.set_fail_closed_on_missing_metadata(fail_closed);
    auto* typed_extension_config =
        config.mutable_fallback_policy()->add_policies()->mutable_typed_extension_config();
    typed_extension_config->set_name("envoy.load_balancing_policies.round_robin");
    RoundRobin rr;
    std::ignore = typed_extension_config->mutable_typed_config()->PackFrom(rr);
    return config;
  }

  void createLoadBalancer(const QuotaAware& config) {
    lb_config_ = factory_.loadConfig(server_factory_context_, config).value();
    thread_aware_lb_ = factory_.create(
        *lb_config_, *cluster_info_, main_thread_priority_set_,
        server_factory_context_.runtime_loader_, server_factory_context_.api_.random_,
        server_factory_context_.time_system_);
    ASSERT_OK(thread_aware_lb_->initialize());
    load_balancer_ = thread_aware_lb_->factory()->create(lb_params_);
    for (uint32_t i = 0; i < thread_local_priority_set_.host_sets_.size(); ++i) {
      thread_local_priority_set_.getMockHostSet(i)->runCallbacks({}, {});
    }
  }

  envoy::config::core::v3::Metadata
  hostMetadata(absl::string_view id, absl::string_view secondary = "",
               absl::string_view ns = "envoy.lb", absl::string_view id_key = "id",
               absl::string_view secondary_key = "secondary_id") {
    envoy::config::core::v3::Metadata metadata;
    Config::Metadata::mutableMetadataValue(metadata, std::string(ns), std::string(id_key))
        .set_string_value(std::string(id));
    if (!secondary.empty()) {
      Config::Metadata::mutableMetadataValue(metadata, std::string(ns), std::string(secondary_key))
          .set_string_value(std::string(secondary));
    }
    return metadata;
  }

  HostSharedPtr makeHost(const std::string& url, absl::string_view id,
                         absl::string_view secondary = "") {
    return Upstream::makeTestHost(cluster_info_, url, hostMetadata(id, secondary));
  }

  uint64_t counterValue(absl::string_view name) {
    return cluster_info_->stats_store_.counterFromString(std::string(name)).value();
  }

  HostSharedPtr makeHostNoMetadata(const std::string& url) {
    return Upstream::makeTestHost(cluster_info_, url);
  }

  void setHosts(uint32_t priority, std::vector<HostSharedPtr> hosts) {
    MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(priority);
    host_set->hosts_ = {hosts.begin(), hosts.end()};
    host_set->healthy_hosts_ = host_set->hosts_;
    host_set->hosts_per_locality_ = Upstream::makeHostsPerLocality({host_set->hosts_});
    host_set->healthy_hosts_per_locality_ = host_set->hosts_per_locality_;
  }

  void setCandidates(const std::vector<std::pair<std::string, std::string>>& pairs) {
    Protobuf::Value list;
    list.mutable_list_value();
    for (const auto& [id, secondary] : pairs) {
      auto* item = list.mutable_list_value()->add_values()->mutable_struct_value();
      (*item->mutable_fields())["id"].set_string_value(id);
      (*item->mutable_fields())["secondary_id"].set_string_value(secondary);
    }
    (*(*stream_info_.metadata_.mutable_filter_metadata())["envoy.filters.http.ratelimit"]
          .mutable_fields())["candidates"] = list;
  }

  void setCandidateStrings(const std::vector<std::string>& ids) {
    Protobuf::Value list;
    for (const auto& id : ids) {
      list.mutable_list_value()->add_values()->set_string_value(id);
    }
    (*(*stream_info_.metadata_.mutable_filter_metadata())["envoy.filters.http.ratelimit"]
          .mutable_fields())["candidates"] = list;
  }

  QuotaAwareLoadBalancerFactory factory_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  std::shared_ptr<NiceMock<Upstream::MockClusterInfo>> cluster_info_{
      std::make_shared<NiceMock<Upstream::MockClusterInfo>>()};
  NiceMock<Upstream::MockPrioritySet> main_thread_priority_set_;
  NiceMock<Upstream::MockPrioritySet> thread_local_priority_set_;
  Upstream::LoadBalancerParams lb_params_{thread_local_priority_set_, nullptr};
  Upstream::LoadBalancerConfigPtr lb_config_;
  Upstream::ThreadAwareLoadBalancerPtr thread_aware_lb_;
  Upstream::LoadBalancerPtr load_balancer_;
  NiceMock<Upstream::MockLoadBalancerContext> load_balancer_context_;
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

TEST_F(QuotaAwareLoadBalancerTest, MissingMetadataFailOpenPicksInner) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east});
  setHosts(1, {west});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(east.get(), response.host.get());
  EXPECT_FALSE(response.failure_status.has_value());
}

TEST_F(QuotaAwareLoadBalancerTest, MissingMetadataFailClosedReturns429) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  setHosts(0, {east});
  createLoadBalancer(makeConfig(true));

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("quota_metadata_missing", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
  EXPECT_EQ(1, counterValue("quota_aware.rq_metadata_missing"));
  EXPECT_EQ(0, counterValue("quota_aware.rq_exhausted"));
}

TEST_F(QuotaAwareLoadBalancerTest, HierarchicalIdMatchesCandidatePrefix) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east, west});
  setCandidates({{"dc1/svc-b", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(west.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, HierarchicalIdDoesNotMatchSiblingPrefix) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a-west/shard/0/member/0");
  setHosts(0, {east});
  setCandidates({{"dc1/svc-a", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("quota_exhausted", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
  EXPECT_EQ(1, counterValue("quota_aware.rq_exhausted"));
}

TEST_F(QuotaAwareLoadBalancerTest, MixedP0SkipsExhaustedHost) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east, west});
  setCandidates({{"dc1/svc-a", ""}});
  createLoadBalancer(makeConfig());

  for (int i = 0; i < 4; ++i) {
    auto response = load_balancer_->chooseHost(&load_balancer_context_);
    ASSERT_NE(nullptr, response.host);
    EXPECT_EQ(east.get(), response.host.get());
  }
}

TEST_F(QuotaAwareLoadBalancerTest, P0ExhaustedPicksP1) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east});
  setHosts(1, {west});
  setCandidates({{"dc1/svc-b", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(west.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, AllExhaustedReturns429) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east});
  setHosts(1, {west});
  setCandidates({});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("quota_exhausted", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
  EXPECT_EQ(1, counterValue("quota_aware.rq_exhausted"));
}

TEST_F(QuotaAwareLoadBalancerTest, UnknownHostMetadataNotSkipped) {
  auto unknown = makeHostNoMetadata("tcp://127.0.0.1:80");
  auto east = makeHost("tcp://127.0.0.1:81", "dc1/svc-a/shard/0/member/0");
  setHosts(0, {unknown, east});
  setCandidates({{"dc1/svc-b", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(unknown.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, InnerRetryPredicateStillFires) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east, west});
  setCandidates({{"dc1/svc-a", ""}, {"dc1/svc-b", ""}});
  ON_CALL(load_balancer_context_, shouldSelectAnotherHost(_))
      .WillByDefault(Invoke([east](const Upstream::Host& host) { return &host == east.get(); }));
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(west.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, StringListCandidates) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east, west});
  setCandidateStrings({"dc1/svc-b"});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(west.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, NoContextFailOpen) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  setHosts(0, {east});
  createLoadBalancer(makeConfig());
  auto response = load_balancer_->chooseHost(nullptr);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(east.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, SamePrimaryDifferentSecondaryPicksLiveMember) {
  auto sku5 = makeHost("tcp://127.0.0.1:80", "prod/svc-x/shard/0/member/0", "sku-5");
  auto sku4 = makeHost("tcp://127.0.0.1:81", "prod/svc-x/shard/0/member/1", "sku-4");
  auto sku3 = makeHost("tcp://127.0.0.1:82", "prod/svc-x/shard/0/member/2", "sku-3");
  setHosts(0, {sku5, sku4, sku3});
  setCandidates({{"prod/svc-x", "sku-4"}, {"prod/svc-x", "sku-3"}});
  createLoadBalancer(makeConfig());

  for (int i = 0; i < 6; ++i) {
    auto response = load_balancer_->chooseHost(&load_balancer_context_);
    ASSERT_NE(nullptr, response.host);
    EXPECT_NE(sku5.get(), response.host.get());
  }
}

TEST_F(QuotaAwareLoadBalancerTest, StampedSecondaryIgnoresEmptySecondaryCandidate) {
  auto sku5 = makeHost("tcp://127.0.0.1:80", "prod/svc-x/shard/0/member/0", "sku-5");
  auto sku4 = makeHost("tcp://127.0.0.1:81", "prod/svc-x/shard/0/member/1", "sku-4");
  setHosts(0, {sku5, sku4});
  setCandidates({{"prod/svc-x", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("quota_exhausted", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
}

TEST_F(QuotaAwareLoadBalancerTest, UnstampedHostsMatchAnyLivePairOnPrimary) {
  auto member0 = makeHost("tcp://127.0.0.1:80", "prod/svc-x/shard/0/member/0");
  auto member1 = makeHost("tcp://127.0.0.1:81", "prod/svc-x/shard/0/member/1");
  setHosts(0, {member0, member1});
  setCandidates({{"prod/svc-x", "sku-4"}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
}

TEST_F(QuotaAwareLoadBalancerTest, DifferentPrimariesSameSecondary) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0", "sku-shared");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1", "sku-shared");
  setHosts(0, {east, west});
  setCandidates({{"dc1/svc-b", "sku-shared"}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(west.get(), response.host.get());
}

// Control plane binds RLS/host wire names onto the generic proto. Same join as
// HierarchicalIdMatchesCandidatePrefix + pair match.
TEST_F(QuotaAwareLoadBalancerTest, ConfiguredWireNamesStillJoin) {
  QuotaAware config = makeConfig();
  config.set_candidates_key("passedBackends");
  config.set_host_metadata_namespace("ext.host");
  config.set_host_id_key("primary_name");
  config.set_host_secondary_id_key("variant");
  config.set_candidate_id_field("primary_name");
  config.set_candidate_secondary_id_field("variant");

  auto live = Upstream::makeTestHost(
      cluster_info_, "tcp://127.0.0.1:80",
      hostMetadata("dc1/svc-b/shard/0/member/1", "sku-4", "ext.host", "primary_name", "variant"));
  auto dead = Upstream::makeTestHost(
      cluster_info_, "tcp://127.0.0.1:81",
      hostMetadata("dc1/svc-a/shard/0/member/0", "sku-5", "ext.host", "primary_name", "variant"));
  setHosts(0, {dead, live});

  Protobuf::Value list;
  auto* item = list.mutable_list_value()->add_values()->mutable_struct_value();
  (*item->mutable_fields())["primary_name"].set_string_value("dc1/svc-b");
  (*item->mutable_fields())["variant"].set_string_value("sku-4");
  (*(*stream_info_.metadata_.mutable_filter_metadata())["envoy.filters.http.ratelimit"]
        .mutable_fields())["passedBackends"] = list;

  createLoadBalancer(config);
  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(live.get(), response.host.get());
}

} // namespace
} // namespace QuotaAware
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
