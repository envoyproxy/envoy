#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/config/subscription.h"
#include "envoy/server/config_tracker.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/logger.h"
#include "source/common/config/resource_type_helper.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

/**
 * An ECDS subscription for a bootstrap module.
 */
class ExtensionConfigSubscription : public Config::SubscriptionCallbacks,
                                    public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  using UpdateCallback = std::function<bool(std::optional<absl::string_view>)>;

  static absl::StatusOr<std::unique_ptr<ExtensionConfigSubscription>>
  create(absl::string_view name, absl::string_view type_url,
         const envoy::config::core::v3::ConfigSource& config_source,
         absl::string_view config_dump_key, Server::Configuration::ServerFactoryContext& context,
         UpdateCallback callback);

  void start();

  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* exception) override;

private:
  ExtensionConfigSubscription(absl::string_view name, absl::string_view type_url,
                              Server::Configuration::ServerFactoryContext& context,
                              UpdateCallback callback);
  absl::Status removeConfig();
  ProtobufTypes::MessagePtr dumpConfig(const Matchers::StringMatcher& name_matcher) const;

  const std::string name_;
  const std::string type_url_;
  Server::Configuration::ServerFactoryContext& context_;
  const UpdateCallback callback_;
  const Config::ResourceTypeHelper<envoy::config::core::v3::TypedExtensionConfig> resource_type_;
  const Stats::ScopeSharedPtr scope_;
  std::unique_ptr<envoy::config::core::v3::TypedExtensionConfig> last_config_;
  std::string last_version_;
  SystemTime last_updated_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  // destroy the subscription before its callbacks and stats
  Config::SubscriptionPtr subscription_;
};

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
