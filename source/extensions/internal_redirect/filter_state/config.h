#pragma once

#include "envoy/extensions/internal_redirect/filter_state/v3/filter_state_config.pb.h"
#include "envoy/extensions/internal_redirect/filter_state/v3/filter_state_config.pb.validate.h"
#include "envoy/router/internal_redirect.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/internal_redirect/filter_state/filter_state.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

class FilterStatePredicateFactory : public Router::InternalRedirectPredicateFactory {
public:
  Router::InternalRedirectPredicateSharedPtr
  createInternalRedirectPredicate(const Protobuf::Message& config, absl::string_view) override {
    const auto& filter_state_config = MessageUtil::downcastAndValidate<
        const envoy::extensions::internal_redirect::filter_state::v3::FilterStateConfig&>(
        config, ProtobufMessage::getStrictValidationVisitor());

    using ProtoConfig = envoy::extensions::internal_redirect::filter_state::v3::FilterStateConfig;
    const bool is_deny = filter_state_config.policy_case() == ProtoConfig::PolicyCase::kDenyValue;
    const std::string& compare_value =
        is_deny ? filter_state_config.deny_value() : filter_state_config.allow_value();

    return std::make_shared<FilterStatePredicate>(filter_state_config.filter_state_key(),
                                                  compare_value, /*follow_on_match=*/!is_deny,
                                                  filter_state_config.allow_if_absent());
  }

  std::string name() const override { return "envoy.internal_redirect_predicates.filter_state"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::internal_redirect::filter_state::v3::FilterStateConfig>();
  }
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
