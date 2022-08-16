#pragma once

#include "envoy/config/typed_config.h"

#include "source/common/common/logger.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * Decides if the target route path matches the provided pattern.
 * Subclassing Logger::Loggable so that implementations can log details.
 */
class PathMatchPredicate : Logger::Loggable<Logger::Id::router> {
public:
  PathMatchPredicate() = default;
  virtual ~PathMatchPredicate() = default;

  /**
   * Used to determine if the current url matches the predicate pattern.
   *
   * @param url current url of route
   * @return true if route url matches the predicate pattern.
   */
  virtual bool match(absl::string_view url) const PURE;

  /**
   * @return the match pattern of the predicate.
   */
  virtual absl::string_view pattern() const PURE;

  /**
   * @return the name of the current predicate.
   */
  virtual absl::string_view name() const PURE;
};

using PathMatchPredicateSharedPtr = std::shared_ptr<PathMatchPredicate>;

/**
 * Factory for PathMatchPredicateFactory.
 */
class PathMatchPredicateFactory : public Envoy::Config::TypedFactory {
public:
  ~PathMatchPredicateFactory() override = default;

  /**
   * @param config contains the proto stored in TypedExtensionConfig for the predicate.
   * @return an PathMatchPredicateSharedPtr.
   */
  virtual absl::StatusOr<PathMatchPredicateSharedPtr>
  createPathMatchPredicate(const Protobuf::Message& config) PURE;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override PURE;

  /**
   * @return the name of the match pattern predicate to be created.
   */
  std::string name() const override PURE;

  /**
   * @return the category of the match pattern predicate to be created.
   */
  std::string category() const override { return "envoy.path.match"; }
};

} // namespace Router
} // namespace Envoy
