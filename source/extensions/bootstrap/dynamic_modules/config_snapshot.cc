#include "source/extensions/bootstrap/dynamic_modules/config_snapshot.h"

#include "envoy/admin/v3/config_dump.pb.h"

#include "source/common/common/matchers.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

absl::StatusOr<std::unique_ptr<ConfigSnapshot>>
ConfigSnapshot::collect(const Server::ConfigTracker& tracker,
                        absl::Span<const absl::string_view> providers) {
  auto snapshot = std::make_unique<ConfigSnapshot>();
  const auto& callbacks = tracker.getCallbacksMap();
  Matchers::UniversalStringMatcher matcher;
  absl::flat_hash_set<absl::string_view> seen;
  TRY_ASSERT_MAIN_THREAD {
    for (absl::string_view provider : providers) {
      if (!seen.insert(provider).second) {
        continue;
      }
      auto it = callbacks.find(std::string(provider));
      if (it == callbacks.end()) {
        continue;
      }
      auto message = it->second(matcher);
      if (message == nullptr) {
        return absl::InternalError("config provider returned no message");
      }
      MessageUtil::redact(*message);
      snapshot->messages_.push_back(std::move(message));
    }
  }
  END_TRY CATCH(const EnvoyException&,
                { return absl::InternalError("config provider collection failed"); });
  return snapshot;
}

absl::StatusOr<std::string> ConfigSnapshot::serialize() const {
  envoy::admin::v3::ConfigDump dump;
  for (const auto& message : messages_) {
    if (!dump.add_configs()->PackFrom(*message)) {
      return absl::InternalError("could not pack config provider message");
    }
  }
  return MessageUtil::getJsonStringFromMessage(dump);
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
