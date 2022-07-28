#pragma once

#include "envoy/config/typed_config.h"

#include "source/common/common/logger.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * Used to decide if path match is needed based on the target route.
 * Subclassing Logger::Loggable so that implementations can log details.
 */
class PathMatchPredicate : Logger::Loggable<Logger::Id::router> {
public:
  PathMatchPredicate() = default;
  virtual ~PathMatchPredicate() = default;

  virtual absl::string_view name() const PURE;

  virtual bool match(absl::string_view pattern) const PURE;

  virtual std::string pattern() const PURE;
};

using PathMatchPredicateSharedPtr = std::shared_ptr<PathMatchPredicate>;

/**
 * Factory for PathMatchPredicateFactory.
 */
class PathMatchPredicateFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~PathMatchPredicateFactory() = default;

  virtual PathMatchPredicateSharedPtr
  createPathMatchPredicate(const Protobuf::Message& config) PURE;

  std::string category() const override { return "envoy.path.match"; }
};

} // namespace Router
} // namespace Envoy
