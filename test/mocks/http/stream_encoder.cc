#include "test/mocks/http/stream_encoder.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Http {

MockStreamEncoder::MockStreamEncoder() {
  ON_CALL(*this, getStream()).WillByDefault(ReturnRef(stream_));
}

MockStreamEncoder::~MockStreamEncoder() = default;

MockRequestEncoder::MockRequestEncoder() {
  ON_CALL(*this, encodeHeaders(_, _))
      .WillByDefault(Invoke([](const RequestHeaderMap& headers, bool) {
        // Check to see that method is not-null. Path can be null for CONNECT and authority can be
        // null at the codec level.
        ASSERT(headers.Method() != nullptr);
      }));
}
MockRequestEncoder::~MockRequestEncoder() = default;

MockResponseEncoder::MockResponseEncoder() {
  ON_CALL(*this, encodeHeaders(_, _))
      .WillByDefault(Invoke([](const ResponseHeaderMap& headers, bool) {
        // Check for passing request headers as response headers in a test.
        ASSERT(headers.Status() != nullptr);
      }));
}
MockResponseEncoder::~MockResponseEncoder() = default;

} // namespace Http
} // namespace Envoy
