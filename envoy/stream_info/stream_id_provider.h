#pragma once

#include <memory>

#include "envoy/common/pure.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace StreamInfo {

/**
 * Stream id for a stream (TCP connection, long-live HTTP2
 * stream, HTTP request, etc.). This  provides string view
 * for logging and tracing and integer view for in modulo,
 * etc. calculations.
 */
class StreamIdProvider {
public:
  virtual ~StreamIdProvider() = default;

  /**
   * @return the optional string view of the stream id.
   */
  virtual absl::optional<absl::string_view> toStringView() const PURE;

  /**
   * @return the optional integer view of the stream id.
   */
  virtual absl::optional<uint64_t> toInteger() const PURE;
};
using StreamIdProviderSharedPtr = std::shared_ptr<StreamIdProvider>;

} // namespace StreamInfo
} // namespace Envoy
