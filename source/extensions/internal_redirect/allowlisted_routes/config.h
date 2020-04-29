#pragma once

#include "envoy/extensions/internal_redirect/allowlisted_routes/v3/allowlisted_routes_config.pb.h"
#include "envoy/extensions/internal_redirect/allowlisted_routes/v3/allowlisted_routes_config.pb.validate.h"
#include "envoy/router/internal_redirect.h"

#include "common/protobuf/message_validator_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/internal_redirect/well_known_names.h"
#include "extensions/internal_redirect/allowlisted_routes/allowlisted_routes.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

class AllowlistedRoutesPredicateFactory : public Router::InternalRedirectPredicateFactory {
public:
  Router::InternalRedirectPredicateSharedPtr
  createInternalRedirectPredicate(const Protobuf::Message& config,
                                  absl::string_view current_route_name) override {
    auto allowlisted_routes_config =
        MessageUtil::downcastAndValidate<const envoy::extensions::internal_redirect::
                                             allowlisted_routes::v3::AllowlistedRoutesConfig&>(
            config, ProtobufMessage::getStrictValidationVisitor());
    return std::make_shared<AllowlistedRoutesPredicate>(current_route_name,
                                                        allowlisted_routes_config);
  }

  std::string name() const override {
    return InternalRedirectPredicateValues::get().AllowlistedRoutesPredicate;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::internal_redirect::allowlisted_routes::v3::AllowlistedRoutesConfig>();
  }
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
