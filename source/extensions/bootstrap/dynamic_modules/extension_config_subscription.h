#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/core/v3/extension.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/server/factory_context.h"

#include "source/common/common/cleanup.h"
#include "source/common/common/logger.h"
#include "source/common/config/resource_type_helper.h"
#include "source/extensions/dynamic_modules/abi/abi.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

class ExtensionConfigSubscriptions;

/**
 * An ECDS subscription for a bootstrap module.
 */
class ExtensionConfigSubscription : public Config::SubscriptionCallbacks,
                                    public Logger::Loggable<Logger::Id::dynamic_modules> {
public:
  using UpdateCallback = std::function<bool(
      absl::string_view name, absl::string_view version, absl::string_view type_url,
      envoy_dynamic_module_type_bootstrap_ecds_config_encoding encoding,
      std::optional<absl::string_view> config)>;

  static absl::StatusOr<std::unique_ptr<ExtensionConfigSubscription>>
  create(absl::string_view name, const envoy::config::core::v3::ExtensionConfigSource& source,
         absl::string_view extension_name, Server::Configuration::ServerFactoryContext& context,
         UpdateCallback callback);
  ~ExtensionConfigSubscription() override;

  void start();

  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                              const std::string& version_info) override;
  absl::Status onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                              const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                              const std::string& system_version_info) override;
  void onConfigUpdateFailed(Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* exception) override;

private:
  ExtensionConfigSubscription(absl::string_view name,
                              const envoy::config::core::v3::ExtensionConfigSource& source,
                              absl::string_view extension_name,
                              Server::Configuration::ServerFactoryContext& context,
                              UpdateCallback callback);
  void removeConfig();
  void dumpConfig(envoy::admin::v3::EcdsConfigDump& dump,
                  const Matchers::StringMatcher& name_matcher) const;

  const std::string name_;
  const std::vector<std::string> type_urls_;
  Server::Configuration::ServerFactoryContext& context_;
  const UpdateCallback callback_;
  const Config::ResourceTypeHelper<envoy::config::core::v3::TypedExtensionConfig> resource_type_;
  const Stats::ScopeSharedPtr scope_;
  std::unique_ptr<envoy::config::core::v3::TypedExtensionConfig> last_config_;
  std::string last_version_;
  SystemTime last_updated_;
  std::shared_ptr<ExtensionConfigSubscriptions> subscriptions_;
  std::unique_ptr<Cleanup> name_reservation_;
  // destroy the subscription before its callbacks and stats
  Config::SubscriptionPtr subscription_;

  friend class ExtensionConfigSubscriptions;
};

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
