#pragma once

#include "envoy/config/typed_config.h"

#include "source/common/common/logger.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * Used to decide if pattern template match or rewrite is needed based on the target route.
 * Subclassing Logger::Loggable so that implementations can log details.
 */
class PatternTemplatePredicate : Logger::Loggable<Logger::Id::router> {
public:
  PatternTemplatePredicate(std::string url_pattern, std::string url_rewrite_pattern)
      : url_pattern_(url_pattern), url_rewrite_pattern_(url_rewrite_pattern){};

  PatternTemplatePredicate() = default;

  virtual ~PatternTemplatePredicate() = default;

  virtual absl::string_view name() const PURE;
  virtual std::string category() const PURE;

  virtual bool match(absl::string_view pattern) const PURE;

  virtual absl::StatusOr<std::string> rewritePattern(absl::string_view current_pattern,
                                                     absl::string_view matched_path) const PURE;

  const std::string url_pattern_;
  const std::string url_rewrite_pattern_;
};

using PatternTemplatePredicateSharedPtr = std::shared_ptr<PatternTemplatePredicate>;

/**
 * Factory for PatternTemplatePredicate.
 */
class PatternTemplatePredicateFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~PatternTemplatePredicateFactory() = default;

  virtual PatternTemplatePredicateSharedPtr
  createUrlTemplatePredicate(std::string url_pattern, std::string url_rewrite_pattern) PURE;

  std::string category() const override { return "envoy.pattern_template"; }
};

} // namespace Router
} // namespace Envoy