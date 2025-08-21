#pragma once

#include <cstdint>

#include "envoy/stream_info/stream_info.h"

#include "source/extensions/filters/network/generic_proxy/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * MatchAction is an enum class that represents the expected action of a matcher.
 */
enum class MatchAction : uint8_t {
  Unknown = 0,
  RouteAction,
};

/**
 * MatchInput is a class that provides the input data for the matchers. It contains the
 * request header frame and the stream info that are used to match the request.
 */
class MatchInput {
public:
  MatchInput(const RequestHeaderFrame& request, const StreamInfo::StreamInfo& stream_info,
             MatchAction expect_action = MatchAction::Unknown)
      : request_header_(request), stream_info_(stream_info), expect_action_(expect_action) {}

  /**
   * @return const RequestHeaderFrame& the request header frame that provides the request
   * attributes.
   */
  const RequestHeaderFrame& requestHeader() const { return request_header_; }

  /**
   * @return const StreamInfo::StreamInfo& the stream info that provides the downstream
   * stream info and attributes that not available in the request header.
   */
  const StreamInfo::StreamInfo& streamInfo() const { return stream_info_; }

  /**
   * @return MatchAction the expected action of the matcher. This allows the embedded matcher
   * to know what action is matched for. This make it is possible to do debug logging or
   * other custom logic based on the expected action.
   */
  MatchAction expectAction() const { return expect_action_; }

  // Used for matcher.
  static constexpr absl::string_view name() { return "generic_proxy_request_input"; }

private:
  const RequestHeaderFrame& request_header_;
  const StreamInfo::StreamInfo& stream_info_;
  const MatchAction expect_action_{};
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
