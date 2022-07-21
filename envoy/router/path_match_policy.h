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
};

using PathMatchPredicateSharedPtr = std::shared_ptr<PathMatchPredicate>;

/**
 * Factory for PatternTemplateMatchPredicate.
 */
class PathMatchPredicateFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~PathMatchPredicateFactory() = default;

  virtual PathMatchPredicateFactory
  createPathMatchPredicate(std::string url_pattern) PURE;

  std::string category() const override { return "envoy.pattern_template"; }
};

/**
 * Used to decide if pattern template rewrite is needed based on the target route.
 * Subclassing Logger::Loggable so that implementations can log details.
 */
class PathRewritePredicate : Logger::Loggable<Logger::Id::router> {
public:
  PathRewritePredicate() = default;
  virtual ~PathRewritePredicate() = default;

  virtual absl::string_view name() const PURE;

  virtual absl::StatusOr<std::string> rewritePattern(absl::string_view current_pattern,
                                           absl::string_view matched_path) const PURE;
};

using PathRewritePredicateSharedPtr = std::shared_ptr<PathRewritePredicate>;

/**
 * Factory for PatternRewriteTemplatePredicate.
 */
class PathRewritePredicateFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~PathRewritePredicateFactory() override = default;

  virtual PathRewritePredicateSharedPtr
  createPathRewritePredicate(std::string url_pattern, std::string url_rewrite_pattern) PURE;

  std::string category() const override { return "envoy.pattern_template"; }
};

} // namespace Router
} // namespace Envoy
