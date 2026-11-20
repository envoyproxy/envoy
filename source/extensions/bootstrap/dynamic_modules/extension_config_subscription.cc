#include "source/extensions/bootstrap/dynamic_modules/extension_config_subscription.h"

#include <algorithm>
#include <utility>

#include "envoy/config/core/v3/config_source.pb.validate.h"
#include "envoy/config/core/v3/extension.pb.validate.h"
#include "envoy/config/xds_manager.h"
#include "envoy/server/admin.h"
#include "envoy/server/config_tracker.h"
#include "envoy/singleton/instance.h"
#include "envoy/singleton/manager.h"

#include "source/common/common/matchers.h"
#include "source/common/config/ecds_subscription_registry.h"
#include "source/common/config/resource_name.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_resource.h"
#include "source/common/protobuf/utility.h"
#include "source/common/stats/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace DynamicModules {

class ExtensionConfigSubscriptions : public Singleton::Instance {
public:
  static std::shared_ptr<ExtensionConfigSubscriptions>
  get(Server::Configuration::ServerFactoryContext& context);

private:
  absl::flat_hash_set<ExtensionConfigSubscription*> subscriptions_;
  Server::ConfigTracker::EntryOwnerPtr config_tracker_entry_;
  friend class ExtensionConfigSubscription;
};

ExtensionConfigSubscription::ExtensionConfigSubscription(
    absl::string_view name, const envoy::config::core::v3::ExtensionConfigSource& source,
    absl::string_view extension_name, Server::Configuration::ServerFactoryContext& context,
    UpdateCallback callback)
    : name_(name), type_urls_(source.type_urls().begin(), source.type_urls().end()),
      context_(context), callback_(std::move(callback)),
      resource_type_(context.messageValidationContext().dynamicValidationVisitor(), "name"),
      scope_(context.scope().createScopeWithTaggedName(
          absl::StrCat("extension_config_discovery.bootstrap.",
                       Stats::Utility::sanitizeStatsName(name)),
          {{"config_name", extension_name.empty() ? "default" : extension_name}},
          absl::StrCat("extension_config_discovery.bootstrap.",
                       Stats::Utility::sanitizeStatsName(name), ".",
                       Stats::Utility::sanitizeStatsName(
                           extension_name.empty() ? "default" : extension_name)))) {}

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
  result->subscriptions_ = ExtensionConfigSubscriptions::get(context);
  result->subscriptions_->subscriptions_.insert(result.get());
  return result;
}

ExtensionConfigSubscription::~ExtensionConfigSubscription() {
  if (subscriptions_ != nullptr) {
    subscriptions_->subscriptions_.erase(this);
  }
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
    return absl::InvalidArgumentError(
        absl::StrCat("bootstrap ECDS resource '", name_, "' requires one configuration"));
  }
  const auto& config = Protobuf::DynamicCastMessage<envoy::config::core::v3::TypedExtensionConfig>(
      resources[0].get().resource());
  auto normalized = Config::XdsResourceIdentifier::normalizeResourceName(config.name());
  if (!normalized.ok() || *normalized != name_ || !config.has_typed_config()) {
    return absl::InvalidArgumentError(absl::StrCat("bootstrap ECDS resource '", name_,
                                                   "' received resource '", config.name(),
                                                   "' with an unexpected name or no config"));
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
        absl::StrCat("bootstrap ECDS resource '", name_, "' received type '", received_type,
                     "', expected ", absl::StrJoin(type_urls_, ", ")));
  }
  auto bytes = MessageUtil::knownAnyToBytes(config.typed_config());
  if (!bytes.ok()) {
    return absl::InvalidArgumentError(absl::StrCat("bootstrap ECDS resource '", name_,
                                                   "' could not decode type '", received_type,
                                                   "': ", bytes.status().message()));
  }
  if (!callback_(name_, version_info, absl::StrCat("type.googleapis.com/", received_type),
                 *bytes)) {
    return absl::InvalidArgumentError(absl::StrCat("bootstrap module rejected ECDS resource '",
                                                   name_, "' of type '", received_type, "'"));
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

void ExtensionConfigSubscription::onConfigUpdateFailed(Config::ConfigUpdateFailureReason reason,
                                                       const EnvoyException*) {
  ENVOY_LOG(debug, "bootstrap ECDS subscription {} failed due to {}", name_,
            static_cast<int>(reason));
}

void ExtensionConfigSubscription::dumpConfig(envoy::admin::v3::EcdsConfigDump& dump,
                                             const Matchers::StringMatcher& name_matcher) const {
  if (last_config_ != nullptr && name_matcher.match(name_)) {
    auto* config = dump.add_ecds_filters();
    MessageUtil::packFrom(*config->mutable_ecds_filter(), *last_config_);
    config->set_version_info(last_version_);
    TimestampUtil::systemClockToTimestamp(last_updated_, *config->mutable_last_updated());
  }
}

SINGLETON_MANAGER_REGISTRATION(dynamic_modules_bootstrap_ecds_subscriptions);

std::shared_ptr<ExtensionConfigSubscriptions>
ExtensionConfigSubscriptions::get(Server::Configuration::ServerFactoryContext& context) {
  auto subscriptions = context.singletonManager().getTyped<ExtensionConfigSubscriptions>(
      SINGLETON_MANAGER_REGISTERED_NAME(dynamic_modules_bootstrap_ecds_subscriptions),
      [] { return std::make_shared<ExtensionConfigSubscriptions>(); });
  if (subscriptions->config_tracker_entry_ == nullptr) {
    if (auto admin = context.admin(); admin.has_value()) {
      subscriptions->config_tracker_entry_ = admin->getConfigTracker().add(
          "ecds_modules", [self = subscriptions.get()](const Matchers::StringMatcher& matcher) {
            auto dump = std::make_unique<envoy::admin::v3::EcdsConfigDump>();
            for (const auto* subscription : self->subscriptions_) {
              subscription->dumpConfig(*dump, matcher);
            }
            return dump;
          });
      RELEASE_ASSERT(subscriptions->config_tracker_entry_ != nullptr,
                     "duplicate bootstrap ECDS config dump key");
    }
  }
  return subscriptions;
}

} // namespace DynamicModules
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
