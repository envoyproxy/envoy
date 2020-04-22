#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

class InternalRedirectPredicate {
public:
  virtual ~InternalRedirectPredicate() = default;

  virtual bool acceptTargetRoute(StreamInfo::FilterState& filter_State,
                                 absl::string_view target_route_name) PURE;
};

using InternalRedirectPredicateSharedPtr = std::shared_ptr<InternalRedirectPredicate>;

class InternalRedirectPredicateFactory : public Config::TypedFactory {
public:
  ~InternalRedirectPredicateFactory() override = default;

  virtual InternalRedirectPredicateSharedPtr
  createInternalRedirectPredicate(const Protobuf::Message& config,
                                  absl::string_view current_route_name) PURE;

  std::string category() const override { return "envoy.internal_redirect_predicates"; }
};

} // namespace Router
} // namespace Envoy
