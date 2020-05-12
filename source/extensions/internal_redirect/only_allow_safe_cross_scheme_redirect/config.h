#pragma once

#include "envoy/extensions/internal_redirect/only_allow_safe_cross_scheme_redirect/v3/only_allow_safe_cross_scheme_redirect_config.pb.h"
#include "envoy/extensions/internal_redirect/only_allow_safe_cross_scheme_redirect/v3/only_allow_safe_cross_scheme_redirect_config.pb.validate.h"
#include "envoy/router/internal_redirect.h"

#include "extensions/internal_redirect/only_allow_safe_cross_scheme_redirect/only_allow_safe_cross_scheme_redirect.h"
#include "extensions/internal_redirect/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

class OnlyAllowSafeCrossSchemeRedirectPredicateFactory
    : public Router::InternalRedirectPredicateFactory {
public:
  Router::InternalRedirectPredicateSharedPtr
  createInternalRedirectPredicate(const Protobuf::Message&,
                                  absl::string_view) override {
    return std::make_shared<OnlyAllowSafeCrossSchemeRedirectPredicate>();
  }

  std::string name() const override {
    return InternalRedirectPredicateValues::get().OnlyAllowSafeCrossSchemeRedirectPredicate;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::internal_redirect::only_allow_safe_cross_scheme_redirect::v3::
            OnlyAllowSafeCrossSchemeRedirectConfig>();
  }
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
