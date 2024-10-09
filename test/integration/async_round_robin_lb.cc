#include "test/integration/async_round_robin_lb.h"

namespace Envoy {
namespace Upstream {

class AsyncRoundRobinLoadBalancer : public RoundRobinLoadBalancer {
public:
  struct AsyncInfo : public AsyncHostSelectionHandle {
    AsyncInfo(HostConstSharedPtr host, LoadBalancerContext* ctx)
        : preselected_host_(host), context_(ctx), detachable_(std::make_shared<Detachable>(this)) {}
    HostConstSharedPtr preselected_host_;
    LoadBalancerContext* context_;

    virtual void cancel() override { detachable_->parent_ = nullptr; }

    // If cancel is called, parent will be nulled out.
    struct Detachable {
      Detachable(AsyncInfo* parent) : parent_(parent) {}
      AsyncInfo* parent_;
    };
    std::shared_ptr<Detachable> detachable_;
  };

  AsyncRoundRobinLoadBalancer(
      const PrioritySet& priority_set, const PrioritySet* local_priority_set, ClusterLbStats& stats,
      Runtime::Loader& runtime, Random::RandomGenerator& random, uint32_t healthy_panic_threshold,
      const envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin&
          round_robin_config,
      TimeSource& time_source, Event::Dispatcher& dispatcher)
      : RoundRobinLoadBalancer(priority_set, local_priority_set, stats, runtime, random,
                               healthy_panic_threshold, round_robin_config, time_source),
        callback_(dispatcher.createSchedulableCallback([this]() { onCallback(); })) {}

  void onCallback() {
    ENVOY_LOG_MISC(debug, "Finishing asynchronous host selection\n");
    for (std::shared_ptr<AsyncInfo::Detachable>& handle : handles_) {
      if (handle->parent_) {
        handle->parent_->context_->onAsyncHostSelection(
            std::move(handle->parent_->preselected_host_));
      }
    }
    handles_.clear();
  }
  HostSelectionResponse chooseHost(LoadBalancerContext* context) override {
    ENVOY_LOG_MISC(debug, "Starting asynchronous host selection\n");
    callback_->scheduleCallbackNextIteration();
    std::unique_ptr<AsyncInfo> info =
        std::make_unique<AsyncInfo>(RoundRobinLoadBalancer::chooseHost(context).host, context);
    handles_.push_back(info->detachable_);
    return {nullptr, std::move(info)};
  }

private:
  std::list<std::shared_ptr<AsyncInfo::Detachable>> handles_;
  Event::SchedulableCallbackPtr callback_;
};

LoadBalancerPtr AsyncRoundRobinCreator::operator()(LoadBalancerParams params,
                                                   OptRef<const LoadBalancerConfig>,
                                                   const ClusterInfo& cluster_info,
                                                   const PrioritySet&, Runtime::Loader& runtime,
                                                   Random::RandomGenerator& random,
                                                   TimeSource& time_source) {

  envoy::extensions::load_balancing_policies::round_robin::v3::RoundRobin config;

  return std::make_unique<AsyncRoundRobinLoadBalancer>(
      params.priority_set, params.local_priority_set, cluster_info.lbStats(), runtime, random,
      PROTOBUF_PERCENT_TO_ROUNDED_INTEGER_OR_DEFAULT(cluster_info.lbConfig(),
                                                     healthy_panic_threshold, 100, 50),
      config, time_source, params.dispatcher_);
}

/**
 * Static registration for the Factory. @see RegisterFactory.
 */
REGISTER_FACTORY(AsyncRoundRobinFactory, TypedLoadBalancerFactory);

} // namespace Upstream
} // namespace Envoy
