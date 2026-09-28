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

  envoy::config::core::v3::Metadata hostMetadata(absl::string_view backend_id,
                                                 absl::string_view model = "") {
    envoy::config::core::v3::Metadata metadata;
    Config::Metadata::mutableMetadataValue(metadata, "aigateway.envoy.io",
                                           "per_route_rule_backend_name")
        .set_string_value(std::string(backend_id));
    if (!model.empty()) {
      Config::Metadata::mutableMetadataValue(metadata, "aigateway.envoy.io", "model_name_override")
          .set_string_value(std::string(model));
    }
    return metadata;
  }

  HostSharedPtr makeHost(const std::string& url, absl::string_view backend_id,
                         absl::string_view model = "") {
    return Upstream::makeTestHost(cluster_info_, url, hostMetadata(backend_id, model));
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

  void setPassedBackends(
      const std::vector<std::pair<std::string, std::string>>& backends_and_models) {
    Protobuf::Value list;
    list.mutable_list_value();
    for (const auto& [backend, model] : backends_and_models) {
      auto* item = list.mutable_list_value()->add_values()->mutable_struct_value();
      (*item->mutable_fields())["backend_name"].set_string_value(backend);
      (*item->mutable_fields())["model_name_override"].set_string_value(model);
    }
    (*(*stream_info_.metadata_.mutable_filter_metadata())["envoy.filters.http.ratelimit"]
          .mutable_fields())["passedBackends"] = list;
  }

  void setPassedBackendStrings(const std::vector<std::string>& backends) {
    Protobuf::Value list;
    for (const auto& backend : backends) {
      list.mutable_list_value()->add_values()->set_string_value(backend);
    }
    (*(*stream_info_.metadata_.mutable_filter_metadata())["envoy.filters.http.ratelimit"]
          .mutable_fields())["passedBackends"] = list;
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
  auto east = makeHost("tcp://127.0.0.1:80", "default/pt-east/route/r/rule/0/ref/0");
  auto west = makeHost("tcp://127.0.0.1:81", "default/pt-west/route/r/rule/0/ref/1");
  setHosts(0, {east});
  setHosts(1, {west});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(east.get(), response.host.get());
  EXPECT_FALSE(response.failure_status.has_value());
}

TEST_F(QuotaAwareLoadBalancerTest, MissingMetadataFailClosedReturns429) {
  auto east = makeHost("tcp://127.0.0.1:80", "default/pt-east/route/r/rule/0/ref/0");
  setHosts(0, {east});
  createLoadBalancer(makeConfig(true));

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("quota_metadata_missing", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
}

TEST_F(QuotaAwareLoadBalancerTest, IdentityJoinFromLongHostId) {
  auto east = makeHost("tcp://127.0.0.1:80", "default/pt-east/route/r/rule/0/ref/0");
  auto west = makeHost("tcp://127.0.0.1:81", "default/pt-west/route/r/rule/0/ref/1");
  setHosts(0, {east, west});
  setPassedBackends({{"default/pt-west", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(west.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, MixedP0SkipsExhaustedHost) {
  auto east = makeHost("tcp://127.0.0.1:80", "default/pt-east/route/r/rule/0/ref/0");
  auto west = makeHost("tcp://127.0.0.1:81", "default/pt-west/route/r/rule/0/ref/1");
  setHosts(0, {east, west});
  setPassedBackends({{"default/pt-east", ""}});
  createLoadBalancer(makeConfig());

  for (int i = 0; i < 4; ++i) {
    auto response = load_balancer_->chooseHost(&load_balancer_context_);
    ASSERT_NE(nullptr, response.host);
    EXPECT_EQ(east.get(), response.host.get());
  }
}

TEST_F(QuotaAwareLoadBalancerTest, P0ExhaustedPicksP1) {
  auto east = makeHost("tcp://127.0.0.1:80", "default/pt-east/route/r/rule/0/ref/0");
  auto west = makeHost("tcp://127.0.0.1:81", "default/pt-west/route/r/rule/0/ref/1");
  setHosts(0, {east});
  setHosts(1, {west});
  setPassedBackends({{"default/pt-west", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(west.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, AllExhaustedReturns429) {
  auto east = makeHost("tcp://127.0.0.1:80", "default/pt-east/route/r/rule/0/ref/0");
  auto west = makeHost("tcp://127.0.0.1:81", "default/pt-west/route/r/rule/0/ref/1");
  setHosts(0, {east});
  setHosts(1, {west});
  setPassedBackends({});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("quota_exhausted", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
}

TEST_F(QuotaAwareLoadBalancerTest, UnknownHostMetadataNotSkipped) {
  auto unknown = makeHostNoMetadata("tcp://127.0.0.1:80");
  auto east = makeHost("tcp://127.0.0.1:81", "default/pt-east/route/r/rule/0/ref/0");
  setHosts(0, {unknown, east});
  setPassedBackends({{"default/pt-west", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(unknown.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, InnerRetryPredicateStillFires) {
  auto east = makeHost("tcp://127.0.0.1:80", "default/pt-east/route/r/rule/0/ref/0");
  auto west = makeHost("tcp://127.0.0.1:81", "default/pt-west/route/r/rule/0/ref/1");
  setHosts(0, {east, west});
  setPassedBackends({{"default/pt-east", ""}, {"default/pt-west", ""}});
  ON_CALL(load_balancer_context_, shouldSelectAnotherHost(_))
      .WillByDefault(Invoke([east](const Upstream::Host& host) { return &host == east.get(); }));
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(west.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, StringListCandidates) {
  auto east = makeHost("tcp://127.0.0.1:80", "default/pt-east/route/r/rule/0/ref/0");
  auto west = makeHost("tcp://127.0.0.1:81", "default/pt-west/route/r/rule/0/ref/1");
  setHosts(0, {east, west});
  setPassedBackendStrings({"default/pt-west"});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(west.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, NoContextFailOpen) {
  auto east = makeHost("tcp://127.0.0.1:80", "default/pt-east/route/r/rule/0/ref/0");
  setHosts(0, {east});
  createLoadBalancer(makeConfig());
  auto response = load_balancer_->chooseHost(nullptr);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(east.get(), response.host.get());
}

TEST_F(QuotaAwareLoadBalancerTest, SameBackendDifferentModelsPicksLiveRef) {
  auto gpt5 = makeHost("tcp://127.0.0.1:80",
                       "nai-admin/openai/route/r/rule/0/ref/0", "gpt-5");
  auto gpt4 = makeHost("tcp://127.0.0.1:81",
                       "nai-admin/openai/route/r/rule/0/ref/1", "gpt-4");
  auto gpt3 = makeHost("tcp://127.0.0.1:82",
                       "nai-admin/openai/route/r/rule/0/ref/2", "gpt-3");
  setHosts(0, {gpt5, gpt4, gpt3});
  setPassedBackends({{"nai-admin/openai", "gpt-4"}, {"nai-admin/openai", "gpt-3"}});
  createLoadBalancer(makeConfig());

  for (int i = 0; i < 6; ++i) {
    auto response = load_balancer_->chooseHost(&load_balancer_context_);
    ASSERT_NE(nullptr, response.host);
    EXPECT_NE(gpt5.get(), response.host.get());
  }
}

TEST_F(QuotaAwareLoadBalancerTest, SameBackendStampedHostIgnoresEmptyModelPair) {
  auto gpt5 = makeHost("tcp://127.0.0.1:80",
                       "nai-admin/openai/route/r/rule/0/ref/0", "gpt-5");
  auto gpt4 = makeHost("tcp://127.0.0.1:81",
                       "nai-admin/openai/route/r/rule/0/ref/1", "gpt-4");
  setHosts(0, {gpt5, gpt4});
  setPassedBackends({{"nai-admin/openai", ""}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  EXPECT_EQ(nullptr, response.host);
  EXPECT_EQ("quota_exhausted", response.details);
  ASSERT_TRUE(response.failure_status.has_value());
  EXPECT_EQ(Http::Code::TooManyRequests, *response.failure_status);
}

TEST_F(QuotaAwareLoadBalancerTest, SameBackendUnstampedHostsMatchAnyLivePair) {
  auto ref0 = makeHost("tcp://127.0.0.1:80", "nai-admin/openai/route/r/rule/0/ref/0");
  auto ref1 = makeHost("tcp://127.0.0.1:81", "nai-admin/openai/route/r/rule/0/ref/1");
  setHosts(0, {ref0, ref1});
  setPassedBackends({{"nai-admin/openai", "gpt-4"}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
}

TEST_F(QuotaAwareLoadBalancerTest, DifferentBackendsSameModel) {
  auto pt = makeHost("tcp://127.0.0.1:80", "default/pt-east/route/r/rule/0/ref/0",
                     "claude-4-sonnet");
  auto od = makeHost("tcp://127.0.0.1:81", "default/od-west/route/r/rule/0/ref/1",
                     "claude-4-sonnet");
  setHosts(0, {pt, od});
  setPassedBackends({{"default/od-west", "claude-4-sonnet"}});
  createLoadBalancer(makeConfig());

  auto response = load_balancer_->chooseHost(&load_balancer_context_);
  ASSERT_NE(nullptr, response.host);
  EXPECT_EQ(od.get(), response.host.get());
}

} // namespace
} // namespace QuotaAware
} // namespace LoadBalancingPolicies
} // namespace Extensions
} // namespace Envoy
