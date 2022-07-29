#pragma once

#include "envoy/config/typed_config.h"

#include "source/common/common/logger.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * Used to decide if pattern template rewrite is needed based on the target route.
 * Subclassing Logger::Loggable so that implementations can log details.
 */
class PathRewritePredicate : Logger::Loggable<Logger::Id::router> {
public:
  PathRewritePredicate() = default;
  virtual ~PathRewritePredicate() = default;

  /**
   * @return the name of the current predicate.
   */
  virtual absl::string_view name() const PURE;

  /**
   * Used to rewrite the current url to the specified output. Can return a failure in case rewrite
   * is not successful.
   *
   * @param current_pattern current url of route
   * @param matched_path pattern to rewrite the url to
   * @return the name of the rewrite pattern current predicate.
   */
  virtual absl::StatusOr<std::string> rewritePattern(absl::string_view current_pattern,
                                                     absl::string_view matched_path) const PURE;

  /**
   * @return the rewrite pattern of the predicate.
   */
  virtual absl::string_view pattern() const PURE;
};

using PathRewritePredicateSharedPtr = std::shared_ptr<PathRewritePredicate>;

/**
 * Factory for PatternRewriteTemplatePredicate.
 */
class PathRewritePredicateFactory : public Envoy::Config::TypedFactory {
public:
  virtual ~PathRewritePredicateFactory() override = default;

  virtual PathRewritePredicateSharedPtr
  createPathRewritePredicate(const Protobuf::Message& rewrite_config) PURE;

  virtual ProtobufTypes::MessagePtr createEmptyConfigProto() override PURE;

  /**
   * @return the name of the rewrite pattern predicate to be created.
   */
  virtual std::string name() const override PURE;

  /**
   * @return the category of the rewrite pattern predicate to be created.
   */
  virtual std::string category() const override PURE;
};

} // namespace Router
} // namespace Envoy
