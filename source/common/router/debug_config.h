#pragma once

#include "envoy/stream_info/filter_state.h"

namespace Envoy {
namespace Router {

/**
 * Configuration for debugging router behavior. The router tries to look up an instance of this
 * class by its `key`, in FilterState (via StreamInfo). If it's not present, debugging features are
 * not enabled.
 *
 * There is currently no public API for populating this configuration -- neither globally nor
 * per-request -- users desiring to use the router debugging features should create and install
 * their own custom StreamDecoderFilter to set DebugConfig as desired before the router consumes
 * it.
 *
 * This is intended to be temporary, and should be replaced by some proper configuration (e.g. in
 * router.proto) when we get unified matchers. See https://github.com/envoyproxy/envoy/issues/5569.
 *
 * TODO(mergeconflict): Keep this promise.
 */
struct DebugConfig : public StreamInfo::FilterState::Object {
  DebugConfig(bool do_not_forward) : do_not_forward_(do_not_forward) {}

  /**
   * @return the string key for finding DebugConfig, if present, in FilterState.
   */
  static absl::string_view key();

  /**
   * Do not forward the associated request to the upstream cluster, if `do_not_forward_` is true.
   * If the router would have forwarded it (assuming all other preconditions are met), it will
   * instead respond with a 204 "no content.".
   */
  bool do_not_forward_{};
};

class DebugConfigFactory : public StreamInfo::FilterState::ObjectFactory {
public:
  std::string name() const override;
  std::unique_ptr<StreamInfo::FilterState::Object>
  createFromBytes(absl::string_view data) const override;
};

} // namespace Router
} // namespace Envoy
