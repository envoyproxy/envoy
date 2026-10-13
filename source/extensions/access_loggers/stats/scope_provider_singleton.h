#pragma once

#include <memory>

#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"
#include "envoy/stats/scope.h"
#include "envoy/type/v3/scope.pb.h"

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Stats {

// ScopeProviderSingleton is a process-wide singleton responsible for managing
// and vending Scopes with limits configuration inline.
class ScopeProviderSingleton;
using ScopeProviderSingletonSharedPtr = std::shared_ptr<ScopeProviderSingleton>;

class ScopeProviderSingleton : public Singleton::Instance {
public:
  static Stats::ScopeSharedPtr
  getScope(Server::Configuration::GenericFactoryContext& factory_context,
           const envoy::type::v3::Scope& config);

  ScopeProviderSingleton() = default;

private:
  absl::flat_hash_map<size_t, std::weak_ptr<Stats::Scope>> scopes_;
};

} // namespace Stats
} // namespace Envoy
