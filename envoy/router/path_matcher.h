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
class PathMatcher : Logger::Loggable<Logger::Id::router> {
public:
  PathMatcher() = default;
  virtual ~PathMatcher() = default;

  /**
   * Returns true if path matches the pattern.
   *
   * @param path the path to be matched
   * @return true if path matches the pattern.
   */
  virtual bool match(absl::string_view path) const PURE;

  /**
   * @return the match uri_template.
   */
  virtual absl::string_view uriTemplate() const PURE;

  /**
   * @return the name of the path matcher.
   */
  virtual absl::string_view name() const PURE;
};

using PathMatcherSharedPtr = std::shared_ptr<PathMatcher>;

/**
 * Factory for PathMatcher.
 */
class PathMatcherFactory : public Envoy::Config::TypedFactory {
public:
  ~PathMatcherFactory() override = default;

  /**
   * @param config contains the proto stored in TypedExtensionConfig.
   * @return an PathMatcherSharedPtr.
   */
  virtual absl::StatusOr<PathMatcherSharedPtr>
  createPathMatcher(const Protobuf::Message& config) PURE;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override PURE;

  /**
   * @return the name of the path matcher to be created.
   */
  std::string name() const override PURE;

  /**
   * @return the category of the path matcher to be created.
   */
  std::string category() const override { return "envoy.path.match"; }
};

} // namespace Router
} // namespace Envoy
