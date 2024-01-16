#include "test/mocks/http/stream_encoder.h"

#include "source/common/http/header_utility.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Http {

MockHttp1StreamEncoderOptions::MockHttp1StreamEncoderOptions() = default;
MockHttp1StreamEncoderOptions::~MockHttp1StreamEncoderOptions() = default;

MockRequestEncoder::MockRequestEncoder() {
  ON_CALL(*this, getStream()).WillByDefault(ReturnRef(stream_));
  ON_CALL(*this, encodeHeaders(_, _))
      .WillByDefault(Invoke([](const RequestHeaderMap& headers, bool) -> Status {
        // Check to see that method is not-null. Path can be null for CONNECT and authority can be
        // null at the codec level.
        ASSERT(HeaderUtility::checkRequiredRequestHeaders(headers).ok());
        return okStatus();
      }));
}
MockRequestEncoder::~MockRequestEncoder() = default;

MockResponseEncoder::MockResponseEncoder() {
  ON_CALL(*this, getStream()).WillByDefault(ReturnRef(stream_));
  ON_CALL(*this, encodeHeaders(_, _))
      .WillByDefault(Invoke([](const ResponseHeaderMap& headers, bool) {
        // Check for passing request headers as response headers in a test.
        ASSERT_NE(nullptr, headers.Status());
      }));
}

MockResponseEncoder::~MockResponseEncoder() {
  // We notify the adapter here to avoid NiceMock dtor from
  // no longer suppressing uninteresting calls.
  if (stream_.codec_callbacks_) {
    stream_.codec_callbacks_->onCodecLowLevelReset();
  }
}

} // namespace Http
} // namespace Envoy
