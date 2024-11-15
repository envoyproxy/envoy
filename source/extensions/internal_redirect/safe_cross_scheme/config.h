#pragma once

#include "envoy/extensions/internal_redirect/safe_cross_scheme/v3/safe_cross_scheme_config.pb.h"
#include "envoy/router/internal_redirect.h"

#include "source/extensions/internal_redirect/safe_cross_scheme/safe_cross_scheme.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

class SafeCrossSchemePredicateFactory : public Router::InternalRedirectPredicateFactory {
public:
  Router::InternalRedirectPredicateSharedPtr
  createInternalRedirectPredicate(const Protobuf::Message&, absl::string_view) override {
    return std::make_shared<SafeCrossSchemePredicate>();
  }

  std::string name() const override {
    return "envoy.internal_redirect_predicates.safe_cross_scheme";
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::internal_redirect::safe_cross_scheme::v3::SafeCrossSchemeConfig>();
  }
};

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
