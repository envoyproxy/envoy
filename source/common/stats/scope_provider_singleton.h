#pragma once

#include <functional>
#include <memory>
#include <string>

#include "envoy/config/metrics/v3/scope.pb.h"
#include "envoy/config/metrics/v3/scope.pb.validate.h"
#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/common/thread.h"
#include "source/common/config/subscription_base.h"
#include "source/common/init/target_impl.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Stats {

// ScopeProviderSingleton is a process-wide singleton responsible for managing
// and vending Scopes with limits configuration via xDS.
class ScopeProviderSingleton;
using ScopeProviderSingletonSharedPtr = std::shared_ptr<ScopeProviderSingleton>;

class ScopeProviderSingleton : public Singleton::Instance {
public:
  class ScopeSubscription;
  using ScopeSubscriptionSharedPtr = std::shared_ptr<ScopeSubscription>;

  class ScopeWrapper {
  public:
    ScopeWrapper(ScopeProviderSingletonSharedPtr provider, ScopeSubscriptionSharedPtr subscription,
                 ScopeSharedPtr scope, std::unique_ptr<Init::TargetImpl> init_target)
        : provider_(provider), subscription_(subscription), scope_(std::move(scope)),
          init_target_(std::move(init_target)) {}

    ~ScopeWrapper();

    ScopeSharedPtr getScope() const { return scope_; }

    void setScope(ScopeSharedPtr scope) {
      scope_ = scope;
      if (init_target_) {
        init_target_->ready();
      }
    }

    ScopeSubscriptionSharedPtr getSubscription() const { return subscription_; }

  private:
    ScopeProviderSingletonSharedPtr provider_;
    ScopeSubscriptionSharedPtr subscription_;
    ScopeSharedPtr scope_;
    std::unique_ptr<Init::TargetImpl> init_target_;
  };
  using ScopeWrapperPtr = std::unique_ptr<ScopeWrapper>;
  using SetScopeCb = std::function<void(ScopeSharedPtr)>;

  static ScopeWrapperPtr
  getScopeWrapper(Server::Configuration::GenericFactoryContext& factory_context,
                  absl::string_view key,
                  const envoy::config::core::v3::ConfigSource& config_source);

  ScopeProviderSingleton(Server::Configuration::ServerFactoryContext& factory_context,
                         const envoy::config::core::v3::ConfigSource& config_source)
      : factory_context_(factory_context), config_source_(config_source),
        scope_(factory_context.scope().createScope("scope_discovery")) {}

  class ScopeSubscription : public Config::SubscriptionBase<envoy::config::metrics::v3::Scope> {
  public:
    explicit ScopeSubscription(ScopeProviderSingleton& parent, absl::string_view resource_name);

    ~ScopeSubscription() override;

    // Config::SubscriptionCallbacks
    absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                const std::string&) override;

    absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                                const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                                const std::string&) override;

    void onConfigUpdateFailed(Config::ConfigUpdateFailureReason, const EnvoyException*) override {}

    void setValidationVisitor(ProtobufMessage::ValidationVisitor& msg_validation_visitor) {
      msg_validation_visitor_ = &msg_validation_visitor;
    }

    void addSetter(intptr_t key, SetScopeCb callback) { setters_[key] = std::move(callback); }

    void removeSetter(intptr_t key) { setters_.erase(key); }

    ScopeSharedPtr getScope() { return scope_; }

    const std::string& resourceName() const { return resource_name_; }

  private:
    friend class ScopeProviderSingleton;
    friend class ScopeWrapper;
    void handleAddedResource(const Config::DecodedResourceRef& resource);
    void handleRemovedResource(absl::string_view resource_name);

    ScopeProviderSingleton& parent_;
    std::string resource_name_;
    Config::SubscriptionPtr subscription_;

    absl::flat_hash_map<intptr_t, SetScopeCb> setters_;
    ScopeSharedPtr scope_;
    ScopeSharedPtr fallback_scope_;

    size_t config_hash_{0};
    ProtobufMessage::ValidationVisitor* msg_validation_visitor_{nullptr};
  };

private:
  Server::Configuration::ServerFactoryContext& factory_context_;
  const envoy::config::core::v3::ConfigSource config_source_;
  Stats::ScopeSharedPtr scope_;
  absl::flat_hash_map<std::string, std::weak_ptr<ScopeSubscription>> subscriptions_;
};

} // namespace Stats
} // namespace Envoy
