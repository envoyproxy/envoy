#include "source/extensions/filters/common/rbac/engine_impl.h"

#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/config/rbac/v3/rbac.pb.validate.h"

#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/common/rbac/matching/data_impl.h"
#include "source/extensions/filters/common/rbac/matching/inputs.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

Envoy::Matcher::ActionFactoryCb
ActionFactory::createActionFactoryCb(const Protobuf::Message& config, ActionContext&,
                                     ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& action_config =
      MessageUtil::downcastAndValidate<const envoy::config::rbac::v3::Action&>(config,
                                                                               validation_visitor);
  const auto& name = action_config.name();
  const auto allow = action_config.allow();

  return [name, allow]() { return std::make_unique<Action>(name, allow); };
}

REGISTER_FACTORY(ActionFactory, Envoy::Matcher::ActionFactory<ActionContext>);

RoleBasedAccessControlEngineImpl::RoleBasedAccessControlEngineImpl(
    const envoy::config::rbac::v3::RBAC& rules,
    ProtobufMessage::ValidationVisitor& validation_visitor, const EnforcementMode mode)
    : action_(rules.action()), mode_(mode) {
  // guard expression builder by presence of a condition in policies
  for (const auto& policy : rules.policies()) {
    if (policy.second.has_condition()) {
      builder_ = Expr::createBuilder(&constant_arena_);
      break;
    }
  }

  for (const auto& policy : rules.policies()) {
    policies_.emplace(policy.first, std::make_unique<PolicyMatcher>(policy.second, builder_.get(),
                                                                    validation_visitor));
  }
}

bool RoleBasedAccessControlEngineImpl::handleAction(const Network::Connection& connection,
                                                    StreamInfo::StreamInfo& info,
                                                    std::string* effective_policy_id) const {
  return handleAction(connection, *Http::StaticEmptyHeaders::get().request_headers, info,
                      effective_policy_id);
}

bool RoleBasedAccessControlEngineImpl::handleAction(const Network::Connection& connection,
                                                    const Envoy::Http::RequestHeaderMap& headers,
                                                    StreamInfo::StreamInfo& info,
                                                    std::string* effective_policy_id) const {
  bool matched = checkPolicyMatch(connection, info, headers, effective_policy_id);

  switch (action_) {
    PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
  case envoy::config::rbac::v3::RBAC::ALLOW:
    return matched;
  case envoy::config::rbac::v3::RBAC::DENY:
    return !matched;
  case envoy::config::rbac::v3::RBAC::LOG: {
    // If not shadow enforcement, set shared log metadata
    if (mode_ != EnforcementMode::Shadow) {
      ProtobufWkt::Struct log_metadata;
      auto& log_fields = *log_metadata.mutable_fields();
      log_fields[DynamicMetadataKeysSingleton::get().AccessLogKey].set_bool_value(matched);
      info.setDynamicMetadata(DynamicMetadataKeysSingleton::get().CommonNamespace, log_metadata);
    }

    return true;
  }
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}

bool RoleBasedAccessControlEngineImpl::checkPolicyMatch(
    const Network::Connection& connection, const StreamInfo::StreamInfo& info,
    const Envoy::Http::RequestHeaderMap& headers, std::string* effective_policy_id) const {
  bool matched = false;

  for (const auto& policy : policies_) {
    if (policy.second->matches(connection, headers, info)) {
      matched = true;
      if (effective_policy_id != nullptr) {
        *effective_policy_id = policy.first;
      }
      break;
    }
  }

  return matched;
}

RoleBasedAccessControlMatcherEngineImpl::RoleBasedAccessControlMatcherEngineImpl(
    const xds::type::matcher::v3::Matcher& matcher,
    Server::Configuration::ServerFactoryContext& factory_context,
    ActionValidationVisitor& validation_visitor, const EnforcementMode) {
  ActionContext context{};
  Envoy::Matcher::MatchTreeFactory<Matching::MatchingData, ActionContext> factory(
      context, factory_context, validation_visitor);
  matcher_ = factory.create(matcher)();

  if (!validation_visitor.errors().empty()) {
    // TODO(snowp): Output all violations.
    throw EnvoyException(fmt::format("requirement violation while creating route match tree: {}",
                                     validation_visitor.errors()[0]));
  }
}

bool RoleBasedAccessControlMatcherEngineImpl::handleAction(const Network::Connection& connection,
                                                           StreamInfo::StreamInfo& info,
                                                           std::string* effective_policy_id) const {
  return handleAction(connection, *Http::StaticEmptyHeaders::get().request_headers, info,
                      effective_policy_id);
}

bool RoleBasedAccessControlMatcherEngineImpl::handleAction(
    const Network::Connection& connection, const Envoy::Http::RequestHeaderMap& headers,
    StreamInfo::StreamInfo& info, std::string* effective_policy_id) const {
  Matching::MatchingDataImpl data(connection, headers, info);
  const auto& result = Envoy::Matcher::evaluateMatch<Matching::MatchingData>(*matcher_, data);
  ASSERT(result.match_state_ == Envoy::Matcher::MatchState::MatchComplete);
  if (result.result_) {
    auto action = result.result_()->getTyped<Action>();
    if (effective_policy_id != nullptr) {
      *effective_policy_id = action.name();
    }

    return action.allow();
  }

  // Default to DENY.
  return false;
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
