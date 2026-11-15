#include "source/extensions/bootstrap/dynamic_modules/config_snapshot.h"

#include "envoy/admin/v3/config_dump.pb.h"

#include "source/common/protobuf/utility.h"

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

absl::StatusOr<std::unique_ptr<ConfigSnapshot>>
ConfigSnapshot::collect(const Server::ConfigTracker& tracker,
                        absl::Span<const absl::string_view> providers,
                        const Matchers::StringMatcher& name_matcher) {
  auto snapshot = std::make_unique<ConfigSnapshot>();
  TRY_ASSERT_MAIN_THREAD {
    // keep callbacks alive if a provider unregisters during collection
    const auto callbacks = tracker.getCallbacksMap();
    for (const auto& [name, callback] : callbacks) {
      if (!absl::c_linear_search(providers, name)) {
        continue;
      }
      auto message = callback(name_matcher);
      if (message == nullptr) {
        return absl::InternalError(absl::StrCat("config provider returned no message: ", name));
      }
      MessageUtil::redact(*message);
      snapshot->messages_.push_back(std::move(message));
    }
  }
  END_TRY CATCH(const EnvoyException& e, { return absl::InternalError(e.what()); });
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
