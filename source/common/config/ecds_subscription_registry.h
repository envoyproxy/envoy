#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "envoy/server/factory_context.h"
#include "envoy/singleton/instance.h"

#include "source/common/common/cleanup.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Config {

/**
 * Prevents exclusive ECDS subscriptions from sharing a resource with another consumer.
 */
class EcdsSubscriptionRegistry : public Singleton::Instance,
                                 public std::enable_shared_from_this<EcdsSubscriptionRegistry> {
public:
  enum class Sharing { Shared, Exclusive };

  static std::shared_ptr<EcdsSubscriptionRegistry>
  get(Server::Configuration::ServerFactoryContext& context);

  absl::StatusOr<std::unique_ptr<Cleanup>> reserve(absl::string_view name, Sharing sharing);

private:
  struct Entry {
    Sharing sharing;
    size_t count;
  };
  absl::flat_hash_map<std::string, Entry> names_;
};

} // namespace Config
} // namespace Envoy
