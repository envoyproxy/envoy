#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/stream_info/stream_info.h"
#include "envoy/upstream/load_balancer.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/router/string_accessor_impl.h"
#include "source/common/upstream/load_balancer_factory_base.h"

#include "test/integration/load_balancers/config.pb.h"

#include "absl/strings/string_view.h"

namespace Envoy {

// Namespace and keys the test load balancer writes on the Check-call stream, and that the
// ext_authz filter under test is configured to propagate to the downstream request.
constexpr absl::string_view CalloutMetadataNamespace = "test.ext_authz.callout";
constexpr absl::string_view CalloutMetadataValue = "from-callout-lb";
constexpr absl::string_view CalloutFilterStateKey = "test.ext_authz.callout_fs";
constexpr absl::string_view CalloutFilterStateValue = "fs-from-callout-lb";

// Stands in for a custom callout-cluster load balancer: on host selection it stamps dynamic
// metadata and a FilterState object onto the Check-call stream, giving the ext_authz
// propagate_call_* fields callout-stream state to copy onto the downstream request.
class CalloutStateLb : public Upstream::LoadBalancer {
public:
  CalloutStateLb(const Upstream::PrioritySet& priority_set) : priority_set_(priority_set) {}

  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override {
    if (context != nullptr && context->requestStreamInfo() != nullptr) {
      auto* stream_info = context->requestStreamInfo();
      Protobuf::Struct metadata;
      (*metadata.mutable_fields())["propagated_value"].set_string_value(
          std::string(CalloutMetadataValue));
      stream_info->setDynamicMetadata(std::string(CalloutMetadataNamespace), metadata);
      stream_info->filterState()->setData(
          CalloutFilterStateKey,
          std::make_shared<Router::StringAccessorImpl>(CalloutFilterStateValue),
          StreamInfo::FilterState::LifeSpan::Request);
    }
    for (const auto& host_set : priority_set_.hostSetsPerPriority()) {
      if (!host_set->hosts().empty()) {
        return {host_set->hosts()[0]};
      }
    }
    return {nullptr};
  }

  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
    return nullptr;
  }
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
    return {};
  }
  std::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext*, const Upstream::Host&,
                           std::vector<uint8_t>&) override {
    return {};
  }

private:
  const Upstream::PrioritySet& priority_set_;
};

class CalloutStateLbFactory : public Upstream::TypedLoadBalancerFactoryBase<
                                  ::test::integration::custom_lb::CustomLbConfig> {
public:
  class EmptyConfig : public Upstream::LoadBalancerConfig {};

  CalloutStateLbFactory()
      : TypedLoadBalancerFactoryBase("envoy.load_balancers.ext_authz_callout_state") {}

  Upstream::ThreadAwareLoadBalancerPtr create(OptRef<const Upstream::LoadBalancerConfig>,
                                              const Upstream::ClusterInfo&,
                                              const Upstream::PrioritySet&, Runtime::Loader&,
                                              Random::RandomGenerator&, TimeSource&) override {
    return std::make_unique<ThreadAwareImpl>();
  }

  absl::StatusOr<Upstream::LoadBalancerConfigPtr>
  loadConfig(Server::Configuration::ServerFactoryContext&, const Protobuf::Message&) override {
    return std::make_unique<EmptyConfig>();
  }

private:
  class ThreadAwareImpl : public Upstream::ThreadAwareLoadBalancer {
  public:
    Upstream::LoadBalancerFactorySharedPtr factory() override {
      return std::make_shared<Factory>();
    }
    absl::Status initialize() override { return absl::OkStatus(); }

    class Factory : public Upstream::LoadBalancerFactory {
    public:
      Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override {
        return std::make_unique<CalloutStateLb>(params.priority_set);
      }
      bool recreateOnHostChangeDeprecated() const override { return false; }
    };
  };
};

} // namespace Envoy
