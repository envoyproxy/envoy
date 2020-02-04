#include "test/mocks/http/stream_encoder.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Http {

MockStreamEncoder::MockStreamEncoder() {
  ON_CALL(*this, getStream()).WillByDefault(ReturnRef(stream_));
}

MockStreamEncoder::~MockStreamEncoder() = default;

MockRequestEncoder::MockRequestEncoder() = default;
MockRequestEncoder::~MockRequestEncoder() = default;

MockResponseEncoder::MockResponseEncoder() {
  ON_CALL(*this, encodeHeaders(_, _)).WillByDefault(Invoke([](const HeaderMap& headers, bool) {
    // The contract is that client codecs must ensure that :status is present.
    ASSERT(headers.Status() != nullptr);
  }));
}
MockResponseEncoder::~MockResponseEncoder() = default;

} // namespace Http
} // namespace Envoy
