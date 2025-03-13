#include "source/common/http/http_option_limits.h"

namespace Envoy {

namespace Http2 {
namespace Utility {

const uint32_t OptionsLimits::MIN_HPACK_TABLE_SIZE;
const uint32_t OptionsLimits::DEFAULT_HPACK_TABLE_SIZE;
const uint32_t OptionsLimits::MAX_HPACK_TABLE_SIZE;
const uint32_t OptionsLimits::MIN_MAX_CONCURRENT_STREAMS;
const uint32_t OptionsLimits::DEFAULT_MAX_CONCURRENT_STREAMS;
const uint32_t OptionsLimits::MAX_MAX_CONCURRENT_STREAMS;
const uint32_t OptionsLimits::MIN_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t OptionsLimits::MAX_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t OptionsLimits::MIN_INITIAL_CONNECTION_WINDOW_SIZE;
const uint32_t OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE;
const uint32_t OptionsLimits::MAX_INITIAL_CONNECTION_WINDOW_SIZE;
const uint32_t OptionsLimits::DEFAULT_MAX_OUTBOUND_FRAMES;
const uint32_t OptionsLimits::DEFAULT_MAX_OUTBOUND_CONTROL_FRAMES;
const uint32_t OptionsLimits::DEFAULT_MAX_CONSECUTIVE_INBOUND_FRAMES_WITH_EMPTY_PAYLOAD;
const uint32_t OptionsLimits::DEFAULT_MAX_INBOUND_PRIORITY_FRAMES_PER_STREAM;
const uint32_t OptionsLimits::DEFAULT_MAX_INBOUND_WINDOW_UPDATE_FRAMES_PER_DATA_FRAME_SENT;

} // namespace Utility
} // namespace Http2

namespace Http3 {
namespace Utility {

const uint32_t OptionsLimits::DEFAULT_INITIAL_STREAM_WINDOW_SIZE;
const uint32_t OptionsLimits::DEFAULT_INITIAL_CONNECTION_WINDOW_SIZE;

} // namespace Utility
} // namespace Http3
  //
} // namespace Envoy
