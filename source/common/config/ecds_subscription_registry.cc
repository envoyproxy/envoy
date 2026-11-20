#include "source/common/config/ecds_subscription_registry.h"

#include "envoy/singleton/manager.h"

#include "source/common/config/xds_resource.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Config {

SINGLETON_MANAGER_REGISTRATION(ecds_subscription_registry);

std::shared_ptr<EcdsSubscriptionRegistry>
EcdsSubscriptionRegistry::get(Server::Configuration::ServerFactoryContext& context) {
  return context.singletonManager().getTyped<EcdsSubscriptionRegistry>(
      SINGLETON_MANAGER_REGISTERED_NAME(ecds_subscription_registry),
      [] { return std::make_shared<EcdsSubscriptionRegistry>(); });
}

absl::StatusOr<std::unique_ptr<Cleanup>> EcdsSubscriptionRegistry::reserve(absl::string_view name,
                                                                           Sharing sharing) {
  auto normalized = XdsResourceIdentifier::normalizeResourceName(name);
  RETURN_IF_NOT_OK(normalized.status());
  auto [it, inserted] = names_.try_emplace(*normalized, Entry{sharing, 0});
  if (!inserted && (sharing == Sharing::Exclusive || it->second.sharing == Sharing::Exclusive)) {
    return absl::AlreadyExistsError(
        absl::StrCat("ECDS resource '", name, "' already has a subscription"));
  }
  ++it->second.count;
  return std::make_unique<Cleanup>([self = shared_from_this(), name = std::move(*normalized)] {
    auto it = self->names_.find(name);
    if (--it->second.count == 0) {
      self->names_.erase(it);
    }
  });
}

} // namespace Config
} // namespace Envoy
