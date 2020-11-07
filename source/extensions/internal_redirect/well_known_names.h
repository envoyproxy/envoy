#pragma once

#include <string>

#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace InternalRedirect {

/**
 * Well-known internal redirect predicate names.
 */
class InternalRedirectPredicatesNameValues {
public:
  const std::string AllowListedRoutesPredicate =
      "envoy.internal_redirect_predicates.allow_listed_routes";
  const std::string PreviousRoutesPredicate = "envoy.internal_redirect_predicates.previous_routes";
  const std::string SafeCrossSchemePredicate =
      "envoy.internal_redirect_predicates.safe_cross_scheme";
};

using InternalRedirectPredicateValues = ConstSingleton<InternalRedirectPredicatesNameValues>;

} // namespace InternalRedirect
} // namespace Extensions
} // namespace Envoy
