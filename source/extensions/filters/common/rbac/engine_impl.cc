#include "source/extensions/filters/common/rbac/engine_impl.h"

#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/config/rbac/v3/rbac.pb.validate.h"

#include "source/common/http/header_map_impl.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

Envoy::Matcher::ActionFactoryCb
ActionFactory::createActionFactoryCb(const Protobuf::Message& config, ActionContext& context,
                                     ProtobufMessage::ValidationVisitor& validation_visitor) {
  const auto& action_config =
      MessageUtil::downcastAndValidate<const envoy::config::rbac::v3::Action&>(config,
                                                                               validation_visitor);
  const auto& name = action_config.name();
  const auto action = action_config.action();

  // If there is at least an action is LOG, we have to go through LOG procedure when handle action.
  if (action == envoy::config::rbac::v3::RBAC::LOG) {
    context.has_log_ = true;
  }

  return [name, action]() { return std::make_unique<Action>(name, action); };
}

REGISTER_FACTORY(ActionFactory, Envoy::Matcher::ActionFactory<ActionContext>);

void generateLog(StreamInfo::StreamInfo& info, EnforcementMode mode, bool log) {
  // If not shadow enforcement, set shared log metadata.
  if (mode != EnforcementMode::Shadow) {
    ProtobufWkt::Struct log_metadata;
    auto& log_fields = *log_metadata.mutable_fields();
    log_fields[DynamicMetadataKeysSingleton::get().AccessLogKey].set_bool_value(log);
    info.setDynamicMetadata(DynamicMetadataKeysSingleton::get().CommonNamespace, log_metadata);
  }
}

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
    generateLog(info, mode_, matched);

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
    ActionValidationVisitor& validation_visitor, const EnforcementMode mode)
    : mode_(mode) {
  ActionContext context{false};
  Envoy::Matcher::MatchTreeFactory<Http::HttpMatchingData, ActionContext> factory(
      context, factory_context, validation_visitor);
  matcher_ = factory.create(matcher)();
  has_log_ = context.has_log_;

  if (!validation_visitor.errors().empty()) {
    throw EnvoyException(fmt::format("requirement violation while creating RBAC match tree: {}",
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
  Http::Matching::HttpMatchingDataImpl data(connection.connectionInfoProvider());
  data.onRequestHeaders(headers);
  const auto& result = Envoy::Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, data);
  ASSERT(result.match_state_ == Envoy::Matcher::MatchState::MatchComplete);
  if (result.result_) {
    auto action = result.result_()->getTyped<Action>();
    if (effective_policy_id != nullptr) {
      *effective_policy_id = action.name();
    }

    // If there is at least an LOG action in matchers, we have to turn on and off for shared log
    // metadata every time when there is a connection or request.
    auto rbac_action = action.action();
    if (has_log_) {
      generateLog(info, mode_, rbac_action == envoy::config::rbac::v3::RBAC::LOG);
    }

    switch (rbac_action) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::config::rbac::v3::RBAC::ALLOW:
    case envoy::config::rbac::v3::RBAC::LOG:
      return true;
    case envoy::config::rbac::v3::RBAC::DENY:
      return false;
    }
  }

  // Default to DENY.
  return false;
}

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
