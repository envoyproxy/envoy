#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/router/path_matcher.h"

#include "source/common/common/logger.h"

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Router {

/**
 * Creates the new route path based on the provided rewrite pattern.
 * Subclassing Logger::Loggable so that implementations can log details.
 */
class PathRewriter : Logger::Loggable<Logger::Id::router> {
public:
  PathRewriter() = default;
  virtual ~PathRewriter() = default;

  /**
   * Determines if the matcher policy is compatible.
   *
   * @param path_match_policy current path match policy for route
   * @return true if current path match policy is acceptable
   */
  virtual absl::Status isCompatiblePathMatcher(PathMatcherSharedPtr path_matcher) const PURE;

  /**
   * Rewrites the current path to the specified output. Return a failure in case rewrite
   * is not successful.
   *
   * @param path current path of route
   * @param rewrite_pattern pattern to rewrite the path to
   * @return the rewritten path.
   */
  virtual absl::StatusOr<std::string> rewritePath(absl::string_view path,
                                                  absl::string_view rewrite_pattern) const PURE;

  /**
   * @return the rewrite uri_template.
   */
  virtual absl::string_view uriTemplate() const PURE;

  /**
   * @return the name of the pattern rewriter.
   */
  virtual absl::string_view name() const PURE;
};

using PathRewriterSharedPtr = std::shared_ptr<PathRewriter>;

/**
 * Factory for PathRewriter.
 */
class PathRewriterFactory : public Envoy::Config::TypedFactory {
public:
  ~PathRewriterFactory() override = default;

  /**
   * @param rewrite_config contains the proto stored in TypedExtensionConfig.
   * @return an PathRewriterSharedPtr.
   */
  virtual absl::StatusOr<PathRewriterSharedPtr>
  createPathRewriter(const Protobuf::Message& rewrite_config) PURE;

  ProtobufTypes::MessagePtr createEmptyConfigProto() override PURE;

  /**
   * @return the name of the pattern rewriter to be created.
   */
  std::string name() const override PURE;

  /**
   * @return the category of the pattern rewriter to be created.
   */
  std::string category() const override { return "envoy.path.rewrite"; }
};

} // namespace Router
} // namespace Envoy
