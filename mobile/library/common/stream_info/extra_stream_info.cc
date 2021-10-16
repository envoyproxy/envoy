#include "library/common/stream_info/extra_stream_info.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace StreamInfo {

const std::string& ExtraStreamInfo::key() {
  CONSTRUCT_ON_FIRST_USE(std::string, "envoy_mobile.extra_stream_info");
}

} // namespace StreamInfo
} // namespace Envoy
