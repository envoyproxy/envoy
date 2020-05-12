#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/stream_info/filter_state.h"

#include "common/common/logger.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * Used to decide if an internal redirect is allowed to be followed based on the target route.
 * Subclassing Logger::Loggable so that implementations can log details.
 */
class InternalRedirectPredicate : Logger::Loggable<Logger::Id::router> {
public:
  virtual ~InternalRedirectPredicate() = default;

  /**
   * A FilterState is provided so that predicate implementation can use it to preserve state across
   * internal redirects.
   *
   * @return whether the route specified by target_route_name is allowed to be followed. Any
   *         predicate returning false will prevent the redirect from being followed, causing the
   *         response to be proxied downstream.
   */
  virtual bool acceptTargetRoute(StreamInfo::FilterState& filter_state,
                                 absl::string_view target_route_name, bool downstream_is_https,
                                 bool target_is_https) PURE;

  /**
   * @return the name of the current predicate.
   */
  virtual absl::string_view name() const PURE;
};

using InternalRedirectPredicateSharedPtr = std::shared_ptr<InternalRedirectPredicate>;

/**
 * Factory for InternalRedirectPredicate.
 */
class InternalRedirectPredicateFactory : public Config::TypedFactory {
public:
  ~InternalRedirectPredicateFactory() override = default;

  /**
   * @return an InternalRedirectPredicate. The given current_route_name is useful for predicates
   *         that need to create per-route FilterState.
   */
  virtual InternalRedirectPredicateSharedPtr
  createInternalRedirectPredicate(const Protobuf::Message& config,
                                  absl::string_view current_route_name) PURE;

  std::string category() const override { return "envoy.internal_redirect_predicates"; }
};

} // namespace Router
} // namespace Envoy
