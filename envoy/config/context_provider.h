#pragma once

#include "envoy/common/callback.h"
#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "xds/core/v3/context_params.pb.h"

namespace Envoy {
namespace Config {

/**
 * A provider for xDS context parameters. These are currently derived from the bootstrap, but will
 * be set dynamically at runtime in the near future as we add support for dynamic context parameter
 * discovery and updates.
 *
 * In general, this is intended to be used only on the main thread, as part of the Server instance
 * interface and config subsystem.
 */
class ContextProvider {
public:
  /**
   * Callback type for when dynamic context changes. The argument provides the resource type URL for
   * the update.
   */
  using UpdateNotificationCb = std::function<absl::Status(absl::string_view)>;

  virtual ~ContextProvider() = default;

  /**
   * @return const xds::core::v3::ContextParams& static node-level context parameters.
   */
  virtual const xds::core::v3::ContextParams& nodeContext() const PURE;

  /**
   * @param resource_type_url resource type URL for context parameters.
   * @return const xds::core::v3::ContextParams& dynamic node-level context parameters.
   */
  virtual const xds::core::v3::ContextParams&
  dynamicContext(absl::string_view resource_type_url) const PURE;

  /**
   * Set a dynamic context parameter.
   * @param resource_type_url resource type URL for context parameter.
   * @param key parameter key.
   * @param value parameter value.
   */
  virtual void setDynamicContextParam(absl::string_view resource_type_url, absl::string_view key,
                                      absl::string_view value) PURE;

  /**
   * Unset a dynamic context parameter.
   * @param resource_type_url resource type URL for context parameter.
   * @param key parameter key.
   */
  virtual void unsetDynamicContextParam(absl::string_view resource_type_url,
                                        absl::string_view key) PURE;

  /**
   * Register a callback for notification when the dynamic context changes.
   * @param callback notification callback.
   * @return Common::CallbackHandlePtr callback handle for removal.
   */
  ABSL_MUST_USE_RESULT virtual Common::CallbackHandlePtr
  addDynamicContextUpdateCallback(UpdateNotificationCb callback) const PURE;
};

} // namespace Config
} // namespace Envoy
