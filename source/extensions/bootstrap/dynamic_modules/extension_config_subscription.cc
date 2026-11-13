#include "source/extensions/bootstrap/dynamic_modules/extension_config_subscription.h"

#include <utility>

#include "envoy/admin/v3/config_dump.pb.h"
#include "envoy/config/xds_manager.h"
#include "envoy/server/admin.h"

#include "source/common/common/matchers.h"
#include "source/common/config/resource_name.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

ExtensionConfigSubscription::ExtensionConfigSubscription(
    absl::string_view name, absl::string_view type_url,
    Server::Configuration::ServerFactoryContext& context, UpdateCallback callback)
    : name_(name), type_url_(type_url), context_(context), callback_(std::move(callback)),
      resource_type_(context.messageValidationContext().dynamicValidationVisitor(), "name"),
      scope_(context.scope().createScope(absl::StrCat("extension_config_discovery.", name, "."))) {}

absl::StatusOr<std::unique_ptr<ExtensionConfigSubscription>> ExtensionConfigSubscription::create(
    absl::string_view name, absl::string_view type_url,
    const envoy::config::core::v3::ConfigSource& config_source, absl::string_view config_dump_key,
    Server::Configuration::ServerFactoryContext& context, UpdateCallback callback) {
  if (name.empty() || type_url.empty()) {
    return absl::InvalidArgumentError("extension config name and type URL must be nonempty");
  }
  auto result = std::unique_ptr<ExtensionConfigSubscription>(
      new ExtensionConfigSubscription(name, type_url, context, std::move(callback)));
  auto subscription = context.xdsManager().subscribeToSingletonResource(
      name, config_source, Config::getTypeUrl<envoy::config::core::v3::TypedExtensionConfig>(),
      *result->scope_, *result, result->resource_type_.resourceDecoder(), {});
  RETURN_IF_NOT_OK(subscription.status());
  result->subscription_ = std::move(subscription.value());
  if (auto admin = context.admin(); admin.has_value()) {
    result->config_tracker_entry_ = admin->getConfigTracker().add(
        std::string(config_dump_key),
        [self = result.get()](const Matchers::StringMatcher& matcher) {
          return self->dumpConfig(matcher);
        });
    if (result->config_tracker_entry_ == nullptr) {
      return absl::AlreadyExistsError(absl::StrCat("bootstrap ECDS config dump entry '",
                                                   config_dump_key, "' is already registered"));
    }
  }
  return result;
}

void ExtensionConfigSubscription::start() { subscription_->start({name_}); }

absl::Status ExtensionConfigSubscription::removeConfig() {
  if (!callback_(std::nullopt)) {
    return absl::InvalidArgumentError("bootstrap module rejected extension config removal");
  }
  last_config_.reset();
  last_version_.clear();
  return absl::OkStatus();
}

absl::Status ExtensionConfigSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& resources, const std::string& version_info) {
  if (resources.empty()) {
    return removeConfig();
  }
  if (resources.size() != 1 || !resources[0].get().hasResource()) {
    return absl::InvalidArgumentError("bootstrap ECDS update must contain one configuration");
  }
  const auto& config = Protobuf::DynamicCastMessage<envoy::config::core::v3::TypedExtensionConfig>(
      resources[0].get().resource());
  if (config.name() != name_ || !config.has_typed_config()) {
    return absl::InvalidArgumentError(
        "bootstrap ECDS resource has an unexpected name or no config");
  }
  if (Config::Utility::getFactoryType(config.typed_config()) !=
      TypeUtil::typeUrlToDescriptorFullName(type_url_)) {
    return absl::InvalidArgumentError(
        "bootstrap ECDS resource has an unexpected configuration type");
  }
  auto bytes = MessageUtil::knownAnyToBytes(config.typed_config());
  RETURN_IF_NOT_OK(bytes.status());
  if (!callback_(*bytes)) {
    return absl::InvalidArgumentError("bootstrap module rejected extension config");
  }
  last_config_ = std::make_unique<envoy::config::core::v3::TypedExtensionConfig>(config);
  last_version_ = version_info;
  last_updated_ = context_.timeSource().systemTime();
  return absl::OkStatus();
}

absl::Status ExtensionConfigSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  if (!removed_resources.empty()) {
    if (removed_resources.size() != 1 || removed_resources[0] != name_ ||
        !added_resources.empty()) {
      return absl::InvalidArgumentError("bootstrap ECDS removal must name only its subscription");
    }
    return removeConfig();
  }
  if (added_resources.empty()) {
    return absl::OkStatus();
  }
  return onConfigUpdate(added_resources, added_resources[0].get().version());
}

void ExtensionConfigSubscription::onConfigUpdateFailed(Config::ConfigUpdateFailureReason,
                                                       const EnvoyException* exception) {
  ENVOY_LOG(warn, "bootstrap ECDS update for {} failed: {}", name_,
            exception == nullptr ? "configuration unavailable" : exception->what());
}

ProtobufTypes::MessagePtr
ExtensionConfigSubscription::dumpConfig(const Matchers::StringMatcher& name_matcher) const {
  auto dump = std::make_unique<envoy::admin::v3::EcdsConfigDump>();
  if (last_config_ != nullptr && name_matcher.match(name_)) {
    auto* config = dump->add_ecds_filters();
    MessageUtil::packFrom(*config->mutable_ecds_filter(), *last_config_);
    config->set_version_info(last_version_);
    TimestampUtil::systemClockToTimestamp(last_updated_, *config->mutable_last_updated());
  }
  return dump;
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
