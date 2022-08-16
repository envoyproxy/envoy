#pragma once

#include "envoy/config/typed_config.h"
#include "envoy/router/path_match.h"

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
   * Used to determine if the matcher policy is compatible.
   *
   * @param path_match_policy current path match policy for route
   * @param active_policy true if user provided policy
   * @return true if current path match policy is acceptable
   */
  virtual absl::Status isCompatibleMatchPolicy(PathMatcherSharedPtr path_match_policy,
                                               bool active_policy) const PURE;

  /**
   * Used to rewrite the current url to the specified output. Can return a failure in case rewrite
   * is not successful.
   *
   * @param url current url of route
   * @param matched_path pattern to rewrite the url to
   * @return the rewritten url.
   */
  virtual absl::StatusOr<std::string> rewriteUrl(absl::string_view current_pattern,
                                                 absl::string_view matched_path) const PURE;

  /**
   * @return the rewrite pattern.
   */
  virtual absl::string_view pattern() const PURE;

  /**
   * @return the name.
   */
  virtual absl::string_view name() const PURE;
};

using PathRewriterSharedPtr = std::shared_ptr<PathRewriter>;

/**
 * Factory for PathRewrite.
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
   * @return the name of the rewrite pattern to be created.
   */
  std::string name() const override PURE;

  /**
   * @return the category of the rewrite pattern to be created.
   */
  std::string category() const override { return "envoy.path.rewrite"; }
};

} // namespace Router
} // namespace Envoy
