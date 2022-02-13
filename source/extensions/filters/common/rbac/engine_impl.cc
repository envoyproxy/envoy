#include "source/extensions/filters/common/rbac/engine_impl.h"

#include "envoy/config/rbac/v3/rbac.pb.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/config/utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/common/expr/custom_cel/custom_cel_vocabulary.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace RBAC {

using CustomCELVocabularyFactory =
    Envoy::Extensions::Filters::Common::Expr::Custom_CEL::CustomCELVocabularyFactory;

RoleBasedAccessControlEngineImpl::RoleBasedAccessControlEngineImpl(
    const envoy::config::rbac::v3::RBAC& rules,
    ProtobufMessage::ValidationVisitor& validation_visitor, const EnforcementMode mode)
    : action_(rules.action()), mode_(mode) {
  // guard expression builder by presence of a condition in policies

  if (rules.has_custom_cel_vocabulary_config()) {
    auto& factory = Envoy::Config::Utility::getAndCheckFactory<CustomCELVocabularyFactory>(
        rules.custom_cel_vocabulary_config());
    auto config = Config::Utility::translateToFactoryConfig(rules.custom_cel_vocabulary_config(),
                                                            validation_visitor, factory);
    custom_cel_vocabulary_ =
        factory.createCustomCELVocabulary(*config, validation_visitor);
  } else {
    custom_cel_vocabulary_ = nullptr;
  }

  for (const auto& policy : rules.policies()) {
    if (policy.second.has_condition()) {
      builder_ = Expr::createBuilder(&constant_arena_, custom_cel_vocabulary_.get());
      break;
    }
  }

  for (const auto& policy : rules.policies()) {
    policies_.emplace(policy.first, std::make_unique<PolicyMatcher>(policy.second, builder_.get(),
                                                                    validation_visitor,
                                                                    custom_cel_vocabulary_.get()));
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

} // namespace RBAC
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
