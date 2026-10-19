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
 * Replaces Envoy's built-in process-wide boolean flag lookup, allowing runtime guards to be
 * evaluated by a custom backend (for example, an external feature management or experimentation
 * system) and to incorporate connection or stream context when available.
 */
class Provider {
public:
  virtual ~Provider() = default;

  /**
   * Evaluate whether a runtime feature is enabled.
   * @param feature the runtime feature name.
   * @return bool true if the feature is enabled.
   */
  virtual bool runtimeFeatureEnabled(absl::string_view feature) const PURE;

  /**
   * Evaluate whether a runtime feature is enabled given connection or stream context.
   * @param feature the runtime feature name.
   * @param stream_info connection or stream StreamInfo providing evaluation context.
   * @return bool true if the feature is enabled.
   */
  virtual bool runtimeFeatureEnabled(absl::string_view feature,
                                     const StreamInfo::StreamInfo& stream_info) const PURE;
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
