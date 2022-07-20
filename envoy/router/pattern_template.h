#pragma once

#include "envoy/config/typed_config.h"

#include "source/common/common/logger.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * Used to decide if pattern template match is needed based on the target route.
 * Subclassing Logger::Loggable so that implementations can log details.
 */
class PatternTemplateMatchPredicate : Logger::Loggable<Logger::Id::router> {
public:
  PatternTemplateMatchPredicate() = default;
  virtual ~PatternTemplateMatchPredicate() = default;

  virtual absl::string_view name() const PURE;

  virtual bool match(absl::string_view pattern) const PURE;
};

using PatternTemplateMatchPredicateSharedPtr = std::shared_ptr<PatternTemplateMatchPredicate>;

/**
 * Factory for PatternTemplateMatchPredicate.
 */
class PatternTemplateMatchPredicateFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~PatternTemplateMatchPredicateFactory() = default;

  virtual PatternTemplateMatchPredicateSharedPtr
  createUrlTemplateMatchPredicate(std::string url_pattern) PURE;

  std::string category() const override { return "envoy.pattern_template"; }
};

/**
 * Used to decide if pattern template rewrite is needed based on the target route.
 * Subclassing Logger::Loggable so that implementations can log details.
 */
class PatternTemplateRewritePredicate : Logger::Loggable<Logger::Id::router> {
public:
  PatternTemplateRewritePredicate() = default;
  virtual ~PatternTemplateRewritePredicate() = default;

  virtual absl::string_view name() const PURE;

  virtual absl::StatusOr<std::string> rewritePattern(absl::string_view current_pattern,
                                           absl::string_view matched_path) const PURE;
};

using PatternTemplateRewritePredicateSharedPtr = std::shared_ptr<PatternTemplateRewritePredicate>;

/**
 * Factory for PatternRewriteTemplatePredicate.
 */
class PatternTemplateRewritePredicateFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~PatternTemplateRewritePredicateFactory() override = default;

  virtual PatternTemplateRewritePredicateSharedPtr
  createUrlTemplateRewritePredicate(std::string url_pattern, std::string url_rewrite_pattern) PURE;

  std::string category() const override { return "envoy.pattern_template"; }
};

} // namespace Router
} // namespace Envoy
