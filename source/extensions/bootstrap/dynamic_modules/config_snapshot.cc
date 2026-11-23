#include "source/extensions/bootstrap/dynamic_modules/config_snapshot.h"

#include "envoy/admin/v3/config_dump.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/common/upstream/endpoint_config_dump.h"

#include "absl/algorithm/container.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

absl::StatusOr<std::unique_ptr<ConfigSnapshot>> ConfigSnapshot::collect(
    const Server::ConfigTracker& tracker, const Upstream::ClusterManager& cluster_manager,
    absl::Span<const absl::string_view> providers, const Matchers::StringMatcher& name_matcher) {
  auto snapshot = std::make_unique<ConfigSnapshot>();
  TRY_ASSERT_MAIN_THREAD {
    std::vector<std::string> selected;
    const auto& callbacks = tracker.getCallbacksMap();
    for (const auto& [name, _] : callbacks) {
      if (absl::c_linear_search(providers, name)) {
        selected.push_back(name);
      }
    }
    const bool include_endpoints = absl::c_linear_search(providers, "endpoint") &&
                                   !callbacks.contains("endpoint") &&
                                   cluster_manager.hasActiveClusters();
    if (include_endpoints) {
      selected.emplace_back("endpoint");
      absl::c_sort(selected);
    }
    for (const auto& name : selected) {
      ProtobufTypes::MessagePtr message;
      if (include_endpoints && name == "endpoint") {
        message = Upstream::dumpEndpointConfigs(cluster_manager, name_matcher);
      } else {
        // an earlier provider may have unregistered or replaced this callback
        const auto it = callbacks.find(name);
        if (it == callbacks.end()) {
          continue;
        }
        // keep the current callback alive if it unregisters itself
        const auto callback = it->second;
        message = callback(name_matcher);
      }
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
