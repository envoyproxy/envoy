#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/load_balancing_policies/host_allowlist/v3/host_allowlist.pb.h"
#include "envoy/extensions/load_balancing_policies/round_robin/v3/round_robin.pb.h"
#include "envoy/http/codes.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/config/metadata.h"
#include "source/extensions/load_balancing_policies/host_allowlist/config.h"
#include "source/extensions/load_balancing_policies/host_allowlist/load_balancer.h"
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

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace LoadBalancingPolicies {
namespace HostAllowlist {
namespace {

using ::envoy::extensions::load_balancing_policies::host_allowlist::v3::HostAllowlist;
using ::envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin;
using ::Envoy::Upstream::HostConstSharedPtr;
using ::Envoy::Upstream::HostSharedPtr;
using ::Envoy::Upstream::MockHostSet;
using ::testing::_;
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRef;

struct InspectingFallbackState {
  HostConstSharedPtr selected_host;
  uint32_t worker_lb_create_count{0};
  bool inspect_context{false};
  std::optional<Upstream::HealthyAndDegradedLoad> original_load;
  std::vector<uint32_t> healthy_load;
  std::vector<uint32_t> degraded_load;
};

class InspectingFallbackLb : public Upstream::ThreadAwareLoadBalancer {
public:
  InspectingFallbackLb(std::shared_ptr<InspectingFallbackState> state,
                       bool recreate_on_host_change);

  Upstream::LoadBalancerFactorySharedPtr factory() override { return factory_; }
  absl::Status initialize() override { return absl::OkStatus(); }

private:
  class WorkerLb : public Upstream::LoadBalancer {
  public:
    WorkerLb(std::shared_ptr<InspectingFallbackState> state,
             const Upstream::PrioritySet& priority_set)
        : state_(std::move(state)), priority_set_(priority_set) {}

    Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override {
      inspect(context);
      return {state_->selected_host};
    }

    HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext* context) override {
      inspect(context);
      return state_->selected_host;
    }

    OptRef<Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
      return {};
    }

    std::optional<Upstream::SelectedPoolAndConnection>
    selectExistingConnection(Upstream::LoadBalancerContext*, const Upstream::Host&,
                             std::vector<uint8_t>&) override {
      return std::nullopt;
    }

  private:
    void inspect(Upstream::LoadBalancerContext* context) {
      if (context == nullptr) {
        return;
      }
      if (state_->original_load.has_value()) {
        const auto& load = context->determinePriorityLoad(
            priority_set_, *state_->original_load, Upstream::RetryPriority::defaultPriorityMapping);
        state_->healthy_load = load.healthy_priority_load_.get();
        state_->degraded_load = load.degraded_priority_load_.get();
      }
      if (!state_->inspect_context) {
        return;
      }
      std::ignore = context->computeHashKey();
      std::ignore = context->metadataMatchCriteria();
      std::ignore = context->downstreamConnection();
      std::ignore = context->requestStreamInfo();
      std::ignore = context->downstreamHeaders();
      std::ignore = context->upstreamSocketOptions();
      std::ignore = context->upstreamTransportSocketOptions();
      std::ignore = context->overrideHostToSelect();
      context->onAsyncHostSelection(HostConstSharedPtr(state_->selected_host), "details");
      context->setHeadersModifier([](Http::ResponseHeaderMap&) {});
    }

    const std::shared_ptr<InspectingFallbackState> state_;
    const Upstream::PrioritySet& priority_set_;
  };

  class WorkerFactory : public Upstream::LoadBalancerFactory {
  public:
    WorkerFactory(std::shared_ptr<InspectingFallbackState> state, bool recreate_on_host_change)
        : state_(std::move(state)), recreate_on_host_change_(recreate_on_host_change) {}

    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override {
      ++state_->worker_lb_create_count;
      return std::make_unique<WorkerLb>(state_, params.priority_set);
    }

    bool recreateOnHostChangeDeprecated() const override { return recreate_on_host_change_; }

  private:
    const std::shared_ptr<InspectingFallbackState> state_;
    const bool recreate_on_host_change_;
  };

  const std::shared_ptr<WorkerFactory> factory_;
};

InspectingFallbackLb::InspectingFallbackLb(std::shared_ptr<InspectingFallbackState> state,
                                           bool recreate_on_host_change)
    : factory_(std::make_shared<WorkerFactory>(std::move(state), recreate_on_host_change)) {}

class HostAllowlistLoadBalancerTest : public ::testing::Test {
public:
  void SetUp() override {
    ON_CALL(load_balancer_context_, requestStreamInfo()).WillByDefault(Return(&stream_info_));
    ON_CALL(load_balancer_context_, determinePriorityLoad(_, _, _))
        .WillByDefault([](const Upstream::PrioritySet&,
                          const Upstream::HealthyAndDegradedLoad& original,
                          const Upstream::RetryPriority::PriorityMappingFunc&)
                           -> const Upstream::HealthyAndDegradedLoad& { return original; });
    ON_CALL(load_balancer_context_, shouldSelectAnotherHost(_)).WillByDefault(Return(false));
    ON_CALL(load_balancer_context_, hostSelectionRetryCount()).WillByDefault(Return(1));
  }

protected:
  HostAllowlist makeConfig(bool fail_closed = false) {
    HostAllowlist config;
    config.set_metadata_namespace("envoy.filters.http.ratelimit");
    config.set_metadata_key("candidates");
    config.add_host_metadata_keys("id");
    config.add_host_metadata_keys("secondary_id");
    config.set_fail_closed_on_missing_metadata(fail_closed);
    auto* typed_extension_config =
        config.mutable_child_policy()->add_policies()->mutable_typed_extension_config();
    typed_extension_config->set_name("envoy.load_balancing_policies.round_robin");
    RoundRobin rr;
    std::ignore = typed_extension_config->mutable_typed_config()->PackFrom(rr);
    return config;
  }

  void createLoadBalancer(const HostAllowlist& config) {
    lb_config_ = factory_.loadConfig(server_factory_context_, config).value();
    thread_aware_lb_ =
        factory_.create(*lb_config_, *cluster_info_, main_thread_priority_set_,
                        server_factory_context_.runtime_loader_,
                        server_factory_context_.api_.random_, server_factory_context_.time_system_);
    ASSERT_OK(thread_aware_lb_->initialize());
    load_balancer_ = thread_aware_lb_->factory()->create(lb_params_);
    for (uint32_t i = 0; i < thread_local_priority_set_.host_sets_.size(); ++i) {
      thread_local_priority_set_.getMockHostSet(i)->runCallbacks({}, {});
    }
  }

  void createLoadBalancerWithFallback(const HostAllowlist& config,
                                      Upstream::ThreadAwareLoadBalancerPtr fallback) {
    lb_config_ = factory_.loadConfig(server_factory_context_, config).value();
    const auto& quota_config = dynamic_cast<const HostAllowlistLbConfig&>(*lb_config_);
    thread_aware_lb_ = std::make_unique<HostAllowlistLoadBalancer>(
        quota_config, std::move(fallback), cluster_info_->statsScope());
    ASSERT_OK(thread_aware_lb_->initialize());
    load_balancer_ = thread_aware_lb_->factory()->create(lb_params_);
  }

  envoy::config::core::v3::Metadata hostMetadata(absl::string_view id,
                                                 absl::string_view secondary = "",
                                                 absl::string_view ns = "envoy.lb",
                                                 absl::string_view id_key = "id",
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

  void setDegradedHosts(uint32_t priority, std::vector<HostSharedPtr> hosts) {
    MockHostSet* host_set = thread_local_priority_set_.getMockHostSet(priority);
    host_set->hosts_ = {hosts.begin(), hosts.end()};
    host_set->healthy_hosts_.clear();
    host_set->degraded_hosts_ = host_set->hosts_;
    host_set->hosts_per_locality_ = Upstream::makeHostsPerLocality({host_set->hosts_});
    host_set->healthy_hosts_per_locality_ = Upstream::makeHostsPerLocality({{}});
    host_set->degraded_hosts_per_locality_ = host_set->hosts_per_locality_;
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

  HostAllowlistLoadBalancerFactory factory_;
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

TEST_F(HostAllowlistLoadBalancerTest, MissingMetadataFailOpenPicksInner) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east});
  setHosts(1, {west});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(east.get(), response.host.get());
  EXPECT_FALSE(response.failure_status.has_value());
  EXPECT_EQ(east, load_balancer_->peekAnotherHost(&load_balancer_context_));
}

TEST_F(HostAllowlistLoadBalancerTest, MissingMetadataFailClosedReturns429) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  setHosts(0, {east});
  createLoadBalancer(makeConfig(true));

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("lb_metadata_missing", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
  EXPECT_EQ(1, counterValue("host_allowlist.rq_metadata_missing"));
  EXPECT_EQ(0, counterValue("host_allowlist.rq_lb_attempt_exhausted"));
  EXPECT_EQ(nullptr, load_balancer_->peekAnotherHost(&load_balancer_context_));
}

TEST_F(HostAllowlistLoadBalancerTest, HierarchicalIdMatchesCandidatePrefix) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east, west});
  setCandidates({{"dc1/svc-b", ""}});
  createLoadBalancer(makeConfig());

  EXPECT_NE(nullptr, load_balancer_->peekAnotherHost(&load_balancer_context_));
  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(west.get(), response.host.get());
}

TEST_F(HostAllowlistLoadBalancerTest, HierarchicalIdDoesNotMatchSiblingPrefix) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a-west/shard/0/member/0");
  setHosts(0, {east});
  setCandidates({{"dc1/svc-a", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("lb_attempt_exhausted", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
  EXPECT_EQ(1, counterValue("host_allowlist.rq_lb_attempt_exhausted"));
}

TEST_F(HostAllowlistLoadBalancerTest, MixedP0SkipsExhaustedHost) {
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

TEST_F(HostAllowlistLoadBalancerTest, P0ExhaustedPicksP1) {
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

TEST_F(HostAllowlistLoadBalancerTest, AllExhaustedReturns429) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east});
  setHosts(1, {west});
  setCandidates({});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("lb_attempt_exhausted", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
  EXPECT_EQ(1, counterValue("host_allowlist.rq_lb_attempt_exhausted"));
}

TEST_F(HostAllowlistLoadBalancerTest, UnknownHostMetadataNotSkipped) {
  auto unknown = makeHostNoMetadata("tcp://127.0.0.1:80");
  auto east = makeHost("tcp://127.0.0.1:81", "dc1/svc-a/shard/0/member/0");
  setHosts(0, {unknown, east});
  setCandidates({{"dc1/svc-b", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(unknown.get(), response.host.get());
}

TEST_F(HostAllowlistLoadBalancerTest, InnerRetryPredicateStillFires) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east, west});
  setCandidates({{"dc1/svc-a", ""}, {"dc1/svc-b", ""}});
  ON_CALL(load_balancer_context_, shouldSelectAnotherHost(_))
      .WillByDefault([east](const Upstream::Host& host) { return &host == east.get(); });
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(west.get(), response.host.get());
}

TEST_F(HostAllowlistLoadBalancerTest, StringListCandidatesAreIgnored) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto west = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {east, west});
  setCandidateStrings({"dc1/svc-b"});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("lb_attempt_exhausted", response.details);
}

TEST_F(HostAllowlistLoadBalancerTest, NoContextFailOpen) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  setHosts(0, {east});
  createLoadBalancer(makeConfig());
  auto response = load_balancer_->chooseHost(nullptr);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(east.get(), response.host.get());
  EXPECT_EQ(east, load_balancer_->peekAnotherHost(nullptr));
}

TEST_F(HostAllowlistLoadBalancerTest, NoContextFailClosed) {
  auto east = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  setHosts(0, {east});
  createLoadBalancer(makeConfig(true));

  auto response = load_balancer_->chooseHost(nullptr);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("lb_metadata_missing", response.details);
  EXPECT_EQ(nullptr, load_balancer_->peekAnotherHost(nullptr));
  EXPECT_EQ(1, counterValue("host_allowlist.rq_metadata_missing"));
}

TEST_F(HostAllowlistLoadBalancerTest, SamePrimaryDifferentSecondaryPicksLiveMember) {
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

TEST_F(HostAllowlistLoadBalancerTest, StampedSecondaryIgnoresEmptySecondaryCandidate) {
  auto sku5 = makeHost("tcp://127.0.0.1:80", "prod/svc-x/shard/0/member/0", "sku-5");
  auto sku4 = makeHost("tcp://127.0.0.1:81", "prod/svc-x/shard/0/member/1", "sku-4");
  setHosts(0, {sku5, sku4});
  setCandidates({{"prod/svc-x", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("lb_attempt_exhausted", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
}

TEST_F(HostAllowlistLoadBalancerTest, UnstampedHostsMatchAnyLivePairOnPrimary) {
  auto member0 = makeHost("tcp://127.0.0.1:80", "prod/svc-x/shard/0/member/0");
  auto member1 = makeHost("tcp://127.0.0.1:81", "prod/svc-x/shard/0/member/1");
  setHosts(0, {member0, member1});
  setCandidates({{"prod/svc-x", "sku-4"}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
}

TEST_F(HostAllowlistLoadBalancerTest, DifferentPrimariesSameSecondary) {
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
TEST_F(HostAllowlistLoadBalancerTest, ConfiguredWireNamesStillJoin) {
  HostAllowlist config = makeConfig();
  config.set_metadata_key("passedBackends");
  config.set_host_metadata_namespace("ext.host");
  config.clear_host_metadata_keys();
  config.add_host_metadata_keys("primary_name");
  config.add_host_metadata_keys("variant");

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

TEST_F(HostAllowlistLoadBalancerTest, MalformedCandidatesAreIgnored) {
  auto live = makeHost("tcp://127.0.0.1:80", "dc1/svc-b/member/0");
  setHosts(0, {live});

  Protobuf::Value list;
  list.mutable_list_value()->add_values()->set_number_value(1);
  list.mutable_list_value()->add_values()->mutable_struct_value();
  auto* valid = list.mutable_list_value()->add_values()->mutable_struct_value();
  (*valid->mutable_fields())["id"].set_string_value("dc1/svc-b");
  (*(*stream_info_.metadata_.mutable_filter_metadata())["envoy.filters.http.ratelimit"]
        .mutable_fields())["candidates"] = list;

  createLoadBalancer(makeConfig());
  EXPECT_EQ(live, load_balancer_->chooseHost(&load_balancer_context_).host);
}

TEST_F(HostAllowlistLoadBalancerTest, EmptyHostIdIsNotQuotaFiltered) {
  auto unstamped = makeHost("tcp://127.0.0.1:80", "");
  setHosts(0, {unstamped});
  setCandidates({{"dc1/svc-b", ""}});
  createLoadBalancer(makeConfig());

  EXPECT_EQ(unstamped, load_balancer_->chooseHost(&load_balancer_context_).host);
}

TEST_F(HostAllowlistLoadBalancerTest, CandidateLongerThanHostDoesNotMatch) {
  auto host = makeHost("tcp://127.0.0.1:80", "dc1/svc-a");
  setHosts(0, {host});
  setCandidates({{"dc1/svc-a/member/0", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("lb_attempt_exhausted", response.details);
}

TEST_F(HostAllowlistLoadBalancerTest, EmptyClusterReturnsNoHost) {
  setHosts(0, {});
  setCandidates({{"dc1/svc-a", ""}});
  auto state = std::make_shared<InspectingFallbackState>();
  createLoadBalancerWithFallback(makeConfig(),
                                 std::make_unique<InspectingFallbackLb>(state, false));

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_FALSE(response.failure_status.has_value());
}

TEST_F(HostAllowlistLoadBalancerTest, ForwardsLoadBalancerContextAndConnectionHooks) {
  auto host = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/member/0");
  setHosts(0, {host});
  setCandidates({{"dc1/svc-a", ""}});

  auto state = std::make_shared<InspectingFallbackState>();
  state->selected_host = host;
  state->inspect_context = true;
  EXPECT_CALL(load_balancer_context_, computeHashKey()).WillOnce(Return(42));
  EXPECT_CALL(load_balancer_context_, metadataMatchCriteria());
  EXPECT_CALL(load_balancer_context_, downstreamConnection());
  EXPECT_CALL(load_balancer_context_, requestStreamInfo())
      .Times(AtLeast(2))
      .WillRepeatedly(Return(&stream_info_));
  EXPECT_CALL(load_balancer_context_, downstreamHeaders());
  EXPECT_CALL(load_balancer_context_, upstreamSocketOptions());
  EXPECT_CALL(load_balancer_context_, upstreamTransportSocketOptions());
  EXPECT_CALL(load_balancer_context_, overrideHostToSelect());
  EXPECT_CALL(load_balancer_context_, onAsyncHostSelection(_, _));
  EXPECT_CALL(load_balancer_context_, setHeadersModifier(_));

  createLoadBalancerWithFallback(makeConfig(),
                                 std::make_unique<InspectingFallbackLb>(state, false));

  EXPECT_EQ(host, load_balancer_->chooseHost(&load_balancer_context_).host);
  EXPECT_FALSE(load_balancer_->lifetimeCallbacks().has_value());
  std::vector<uint8_t> hash_key;
  EXPECT_FALSE(load_balancer_->selectExistingConnection(&load_balancer_context_, *host, hash_key)
                   .has_value());
}

TEST_F(HostAllowlistLoadBalancerTest, ChildPickRejectedIfNotOnAllowlist) {
  auto dead = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/shard/0/member/0");
  auto live = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/shard/0/member/1");
  setHosts(0, {dead, live});
  setCandidates({{"dc1/svc-b", ""}});

  auto state = std::make_shared<InspectingFallbackState>();
  state->selected_host = dead;
  createLoadBalancerWithFallback(makeConfig(),
                                 std::make_unique<InspectingFallbackLb>(state, false));

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("lb_attempt_exhausted", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
  EXPECT_EQ(1, counterValue("host_allowlist.rq_lb_attempt_exhausted"));
}

TEST_F(HostAllowlistLoadBalancerTest, RecreatesLegacyFallbackOnHostUpdate) {
  setHosts(0, {});
  auto state = std::make_shared<InspectingFallbackState>();
  createLoadBalancerWithFallback(makeConfig(), std::make_unique<InspectingFallbackLb>(state, true));
  ASSERT_EQ(1, state->worker_lb_create_count);

  thread_local_priority_set_.runUpdateCallbacks(0, {}, {});
  EXPECT_EQ(2, state->worker_lb_create_count);
}

TEST_F(HostAllowlistLoadBalancerTest, RedistributesRoundingRemainderToFirstHealthyPriority) {
  auto state = std::make_shared<InspectingFallbackState>();
  for (uint32_t priority = 0; priority < 3; ++priority) {
    auto host = makeHost(absl::StrCat("tcp://127.0.0.1:", 80 + priority),
                         absl::StrCat("dc1/svc-", priority, "/member/0"));
    setHosts(priority, {host});
    if (priority == 0) {
      state->selected_host = host;
    }
  }
  setCandidates({{"dc1/svc-0", ""}, {"dc1/svc-1", ""}, {"dc1/svc-2", ""}});
  state->original_load = Upstream::HealthyAndDegradedLoad{Upstream::HealthyLoad({1, 1, 1}),
                                                          Upstream::DegradedLoad({0, 0, 0})};
  createLoadBalancerWithFallback(makeConfig(),
                                 std::make_unique<InspectingFallbackLb>(state, false));

  EXPECT_NE(nullptr, load_balancer_->chooseHost(&load_balancer_context_).host);
  EXPECT_EQ((std::vector<uint32_t>{34, 33, 33}), state->healthy_load);
}

TEST_F(HostAllowlistLoadBalancerTest, RedistributesZeroLoadToLiveDegradedPriority) {
  auto exhausted = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/member/0");
  auto degraded = makeHost("tcp://127.0.0.1:81", "dc1/svc-b/member/0");
  setHosts(0, {exhausted});
  setDegradedHosts(1, {degraded});
  setCandidates({{"dc1/svc-b", ""}});

  auto state = std::make_shared<InspectingFallbackState>();
  state->selected_host = degraded;
  state->original_load = Upstream::HealthyAndDegradedLoad{Upstream::HealthyLoad({0, 0}),
                                                          Upstream::DegradedLoad({0, 0})};
  createLoadBalancerWithFallback(makeConfig(),
                                 std::make_unique<InspectingFallbackLb>(state, false));

  EXPECT_EQ(degraded, load_balancer_->chooseHost(&load_balancer_context_).host);
  EXPECT_EQ((std::vector<uint32_t>{0, 100}), state->degraded_load);
}

TEST_F(HostAllowlistLoadBalancerTest, RedistributesRoundingRemainderToFirstDegradedPriority) {
  auto state = std::make_shared<InspectingFallbackState>();
  for (uint32_t priority = 0; priority < 3; ++priority) {
    auto host = makeHost(absl::StrCat("tcp://127.0.0.1:", 80 + priority),
                         absl::StrCat("dc1/svc-", priority, "/member/0"));
    setDegradedHosts(priority, {host});
    if (priority == 0) {
      state->selected_host = host;
    }
  }
  setCandidates({{"dc1/svc-0", ""}, {"dc1/svc-1", ""}, {"dc1/svc-2", ""}});
  state->original_load = Upstream::HealthyAndDegradedLoad{Upstream::HealthyLoad({0, 0, 0}),
                                                          Upstream::DegradedLoad({1, 1, 1})};
  createLoadBalancerWithFallback(makeConfig(),
                                 std::make_unique<InspectingFallbackLb>(state, false));

  EXPECT_NE(nullptr, load_balancer_->chooseHost(&load_balancer_context_).host);
  EXPECT_EQ((std::vector<uint32_t>{34, 33, 33}), state->degraded_load);
}

TEST_F(HostAllowlistLoadBalancerTest, PeekWithNoQuotaAvailableHostLeavesLoadAtZero) {
  auto host = makeHost("tcp://127.0.0.1:80", "dc1/svc-a/member/0");
  setHosts(0, {host});
  setCandidates({});

  auto state = std::make_shared<InspectingFallbackState>();
  state->original_load =
      Upstream::HealthyAndDegradedLoad{Upstream::HealthyLoad({0}), Upstream::DegradedLoad({0})};
  createLoadBalancerWithFallback(makeConfig(),
                                 std::make_unique<InspectingFallbackLb>(state, false));

  EXPECT_EQ(nullptr, load_balancer_->peekAnotherHost(&load_balancer_context_));
  EXPECT_EQ((std::vector<uint32_t>{0}), state->healthy_load);
  EXPECT_EQ((std::vector<uint32_t>{0}), state->degraded_load);
}

} // namespace
} // namespace HostAllowlist
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
