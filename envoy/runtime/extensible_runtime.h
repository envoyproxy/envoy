#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace StreamInfo {
class StreamInfo;
} // namespace StreamInfo

namespace ExtensibleRuntime {

/**
 * Interface for custom runtime feature evaluation.
 *
 * Unlike standard process-wide runtime lookups in Runtime::Snapshot, a Provider can make
 * context-aware decisions using connection or stream state from StreamInfo, and can be notified
 * when a feature flag evaluation actively alters execution behavior.
 */
class Provider {
public:
  virtual ~Provider() = default;

  /**
   * Check whether a runtime feature is enabled in a process-wide or request-unaware context.
   * @param feature the runtime feature key to look up.
   * @return bool true if the feature is enabled.
   */
  virtual bool runtimeFeatureEnabled(absl::string_view feature) const PURE;

  /**
   * Check whether a runtime feature is enabled for the connection associated with the given
   * StreamInfo. Implementations may inspect connection properties or cache evaluation state on
   * stream_info (e.g., via filterState()).
   * @param feature the runtime feature key to look up.
   * @param stream_info the connection's StreamInfo.
   * @return bool true if the feature is enabled for this connection.
   */
  virtual bool perConnectionRuntimeFeatureEnabled(absl::string_view feature,
                                                  StreamInfo::StreamInfo& stream_info) const PURE;

  /**
   * Check whether a runtime feature is enabled for the request or stream associated with the
   * given StreamInfo. Implementations may inspect request attributes or store evaluation state on
   * stream_info (e.g., via filterState()).
   * @param feature the runtime feature key to look up.
   * @param stream_info the request's StreamInfo.
   * @return bool true if the feature is enabled for this request.
   */
  virtual bool perRequestRuntimeFeatureEnabled(absl::string_view feature,
                                               StreamInfo::StreamInfo& stream_info) const PURE;

  /**
   * Record that the connection or request associated with stream_info hit a condition where
   * enabling or disabling the given runtime feature changes Envoy's behavior (even if the feature
   * is currently disabled). For example:
   *
   *   if (condition_affected_by_feature) {
   *     ExtensibleRuntime::recordBehaviorChange(
   *         "envoy.reloadable_features.my_feature", stream_info);
   *     if (ExtensibleRuntime::perRequestRuntimeFeatureEnabled(
   *             "envoy.reloadable_features.my_feature", stream_info)) {
   *       newBehavior();
   *     } else {
   *       oldBehavior();
   *     }
   *   }
   *
   * @param feature the runtime feature key whose guarded condition was reached.
   * @param stream_info the connection or request StreamInfo affected by the feature.
   */
  virtual void recordBehaviorChange(absl::string_view feature,
                                    StreamInfo::StreamInfo& stream_info) const PURE;
};

using ProviderPtr = std::unique_ptr<Provider>;

/**
 * Implemented for each ExtensibleRuntime::Provider and registered via Registry::registerFactory or
 * the convenience class RegisterFactory.
 */
class ProviderFactory : public Config::TypedFactory {
public:
  ~ProviderFactory() override = default;

  /**
   * Create a particular ExtensibleRuntime::Provider implementation from a config proto.
   * @param config the custom configuration for this extensible runtime provider type.
   * @return ProviderPtr the initialized extensible runtime provider.
   */
  virtual ProviderPtr createProvider(const Protobuf::Message& config) PURE;

  std::string category() const override { return "envoy.extensible_runtime"; }
};

} // namespace ExtensibleRuntime
} // namespace Envoy
