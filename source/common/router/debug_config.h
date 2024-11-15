#pragma once

#include <string>

#include "envoy/http/header_map.h"
#include "envoy/stream_info/filter_state.h"

#include "absl/types/optional.h"

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

  DebugConfig(bool append_cluster, absl::optional<Http::LowerCaseString> cluster_header,
              bool append_upstream_host, absl::optional<Http::LowerCaseString> hostname_header,
              absl::optional<Http::LowerCaseString> host_address_header, bool do_not_forward,
              absl::optional<Http::LowerCaseString> not_forwarded_header);

  /**
   * @return the string key for finding DebugConfig, if present, in FilterState.
   */
  static const std::string& key();

  /**
   * Append cluster information as a response header if `append_cluster_` is true. The router will
   * use `cluster_header_` as the header name, if specified, or 'x-envoy-cluster' by default.
   */
  bool append_cluster_{};
  absl::optional<Http::LowerCaseString> cluster_header_;

  /**
   * Append upstream host name and address as response headers, if `append_upstream_host_` is true.
   * The router will use `hostname_header_` and `host_address_header_` as the header names, if
   * specified, or 'x-envoy-upstream-hostname' and 'x-envoy-upstream-host-address' by default.
   */
  bool append_upstream_host_{};
  absl::optional<Http::LowerCaseString> hostname_header_;
  absl::optional<Http::LowerCaseString> host_address_header_;

  /**
   * Do not forward the associated request to the upstream cluster, if `do_not_forward_` is true.
   * If the router would have forwarded it (assuming all other preconditions are met), it will
   * instead respond with a 204 "no content." Append `not_forwarded_header_`, if specified, or
   * 'x-envoy-not-forwarded' by default. Any debug headers specified above (or others introduced by
   * other filters) will be appended to this empty response.
   */
  bool do_not_forward_{};
  absl::optional<Http::LowerCaseString> not_forwarded_header_;
};

} // namespace Router
} // namespace Envoy
