#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

class InternalRedirectTargetRoutePredicate {
public:
  virtual ~InternalRedirectTargetRoutePredicate() = default;

  virtual bool acceptTargetRoute(StreamInfo::FilterState& filter_State,
                                 absl::string_view route_name) PURE;

  virtual void onTargetRouteAccepted(StreamInfo::FilterState& filter_state,
                                     absl::string_view route_name) PURE;
};

using InternalRedirectTargetRoutePredicateSharedPtr =
    std::shared_ptr<InternalRedirectTargetRoutePredicate>;

class InternalRedirectTargetRoutePredicateFactory : public Config::TypedFactory {
public:
  ~InternalRedirectTargetRoutePredicateFactory() override = default;

  virtual InternalRedirectTargetRoutePredicateSharedPtr createInternalRedirectTargetRoutePredicate(
      const Protobuf::Message& config, ProtobufMessage::ValidationVisitor& validation_visitor) PURE;
};

} // namespace Router
} // namespace Envoy
