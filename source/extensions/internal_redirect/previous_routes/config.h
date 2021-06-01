#pragma once

#include "envoy/extensions/internal_redirect/previous_routes/v3/previous_routes_config.pb.h"
#include "envoy/extensions/internal_redirect/previous_routes/v3/previous_routes_config.pb.validate.h"
#include "envoy/router/internal_redirect.h"

#include "extensions/internal_redirect/previous_routes/previous_routes.h"
#include "extensions/internal_redirect/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

class PreviousRoutesPredicateFactory : public Router::InternalRedirectPredicateFactory {
public:
  Router::InternalRedirectPredicateSharedPtr
  createInternalRedirectPredicate(const Protobuf::Message&,
                                  absl::string_view current_route_name) override {
    return std::make_shared<PreviousRoutesPredicate>(current_route_name);
  }

  std::string name() const override {
    return InternalRedirectPredicateValues::get().PreviousRoutesPredicate;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::internal_redirect::previous_routes::v3::PreviousRoutesConfig>();
  }
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
