#include "source/extensions/bootstrap/dynamic_modules/extension_config_subscription.h"

#include <algorithm>
#include <utility>

#include "envoy/config/core/v3/config_source.pb.validate.h"
#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/config/xds_manager.h"
#include "envoy/server/admin.h"

#include "source/common/common/matchers.h"
#include "source/common/config/ecds_subscription_registry.h"
#include "source/common/config/resource_name.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_resource.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

ExtensionConfigSubscription::ExtensionConfigSubscription(
    absl::string_view name, const envoy::config::core::v3::ExtensionConfigSource& source,
    absl::string_view, Server::Configuration::ServerFactoryContext& context,
    UpdateCallback callback)
    : name_(name), type_urls_(source.type_urls().begin(), source.type_urls().end()),
      context_(context), callback_(std::move(callback)),
      resource_type_(context.messageValidationContext().dynamicValidationVisitor(), "name"),
      scope_(context.scope().createScope(absl::StrCat("extension_config_discovery.", name, "."))) {}

absl::StatusOr<std::unique_ptr<ExtensionConfigSubscription>> ExtensionConfigSubscription::create(
    absl::string_view name, const envoy::config::core::v3::ExtensionConfigSource& source,
    absl::string_view extension_name, Server::Configuration::ServerFactoryContext& context,
    UpdateCallback callback) {
  if (name.empty() || name == "*") {
    return absl::InvalidArgumentError("bootstrap ECDS requires a single resource name");
  }
  auto normalized = Config::XdsResourceIdentifier::normalizeResourceName(name);
  RETURN_IF_NOT_OK(normalized.status());
  if (Config::XdsResourceIdentifier::hasXdsTpScheme(name)) {
    auto urn = Config::XdsResourceIdentifier::decodeUrn(name);
    RETURN_IF_NOT_OK(urn.status());
    if (absl::EndsWith(urn->id(), "*")) {
      return absl::InvalidArgumentError("bootstrap ECDS does not support wildcard resource names");
    }
  }
  MessageUtil::validate(source, context.messageValidationContext().dynamicValidationVisitor());
  MessageUtil::validate(source.config_source(),
                        context.messageValidationContext().dynamicValidationVisitor());
  if (std::any_of(source.type_urls().begin(), source.type_urls().end(),
                  [](const auto& type) { return type.empty(); })) {
    return absl::InvalidArgumentError("bootstrap ECDS payload type URLs must be nonempty");
  }
  if (source.has_default_config() || source.apply_default_config_without_warming()) {
    return absl::InvalidArgumentError("bootstrap ECDS does not support default configurations");
  }
  auto result = std::unique_ptr<ExtensionConfigSubscription>(new ExtensionConfigSubscription(
      *normalized, source, extension_name, context, std::move(callback)));
  if (source.config_source().has_ads() || Config::XdsResourceIdentifier::hasXdsTpScheme(name)) {
    auto reservation = Config::EcdsSubscriptionRegistry::get(context)->reserve(
        *normalized, Config::EcdsSubscriptionRegistry::Sharing::Exclusive);
    RETURN_IF_NOT_OK(reservation.status());
    result->name_reservation_ = std::move(*reservation);
  }
  auto subscription = context.xdsManager().subscribeToSingletonResource(
      *normalized, source.config_source(),
      Config::getTypeUrl<envoy::config::core::v3::TypedExtensionConfig>(), *result->scope_, *result,
      result->resource_type_.resourceDecoder(), {});
  RETURN_IF_NOT_OK(subscription.status());
  result->subscription_ = std::move(*subscription);
  if (auto admin = context.admin(); admin.has_value()) {
    auto& tracker = admin->getConfigTracker();
    const auto config_dump_key = absl::StrCat("ecds_bootstrap.", extension_name);
    std::string key(config_dump_key);
    for (uint64_t suffix = 1; tracker.getCallbacksMap().contains(key); ++suffix) {
      key = absl::StrCat(config_dump_key, ".", suffix);
    }
    result->config_tracker_entry_ =
        tracker.add(key, [self = result.get()](const Matchers::StringMatcher& matcher) {
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

void ExtensionConfigSubscription::removeConfig() {
  if (!callback_(name_, "", "", std::nullopt)) {
    ENVOY_LOG(warn, "bootstrap module rejected ECDS removal for {}", name_);
    return;
  }
  last_config_.reset();
  last_version_.clear();
}

absl::Status ExtensionConfigSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& resources, const std::string& version_info) {
  // an omitted ECDS resource or TTL heartbeat does not remove its configuration
  if (resources.empty()) {
    return absl::OkStatus();
  }
  if (resources.size() != 1 || !resources[0].get().hasResource()) {
    return absl::InvalidArgumentError("bootstrap ECDS update must contain one configuration");
  }
  const auto& config = Protobuf::DynamicCastMessage<envoy::config::core::v3::TypedExtensionConfig>(
      resources[0].get().resource());
  auto normalized = Config::XdsResourceIdentifier::normalizeResourceName(config.name());
  if (!normalized.ok() || *normalized != name_ || !config.has_typed_config()) {
    return absl::InvalidArgumentError(
        "bootstrap ECDS resource has an unexpected name or no config");
  }
  if (last_config_ != nullptr &&
      last_config_->typed_config().type_url() == config.typed_config().type_url() &&
      last_config_->typed_config().value() == config.typed_config().value()) {
    return absl::OkStatus();
  }
  const auto received_type = Config::Utility::getFactoryType(config.typed_config());
  if (std::none_of(type_urls_.begin(), type_urls_.end(), [&received_type](const auto& type) {
        return TypeUtil::typeUrlToDescriptorFullName(type) == received_type;
      })) {
    return absl::InvalidArgumentError(
        "bootstrap ECDS resource has an unexpected configuration type");
  }
  auto bytes = MessageUtil::knownAnyToBytes(config.typed_config());
  RETURN_IF_NOT_OK(bytes.status());
  if (!callback_(name_, version_info, absl::StrCat("type.googleapis.com/", received_type),
                 *bytes)) {
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
  if (!added_resources.empty()) {
    return onConfigUpdate(added_resources, added_resources[0].get().version());
  }
  for (const auto& name : removed_resources) {
    auto normalized = Config::XdsResourceIdentifier::normalizeResourceName(name);
    if (normalized.ok() && *normalized == name_) {
      removeConfig();
      break;
    }
  }
  return absl::OkStatus();
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
