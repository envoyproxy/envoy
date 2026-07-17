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
    using ProtoConfig = envoy::extensions::internal_redirect::filter_state::v3::FilterStateConfig;
    const auto& filter_state_config = MessageUtil::downcastAndValidate<const ProtoConfig&>(
        config, ProtobufMessage::getStrictValidationVisitor());

    return std::make_shared<FilterStatePredicate>(filter_state_config.redirect_enabled_key(),
                                                  filter_state_config.redirect_if_absent());
  }

  std::string name() const override { return "envoy.internal_redirect_predicates.filter_state"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<ProtoConfig>();
  }
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
