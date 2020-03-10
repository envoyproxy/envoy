#include "test/mocks/http/stream_decoder.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Http {

MockStreamDecoder::MockStreamDecoder() = default;
MockStreamDecoder::~MockStreamDecoder() = default;

MockRequestDecoder::MockRequestDecoder() {
  ON_CALL(*this, decodeHeaders_(_, _)).WillByDefault(Invoke([](RequestHeaderMapPtr& headers, bool) {
    // Check to see that method is not-null. Path can be null for CONNECT and authority can be null
    // at the codec level.
    ASSERT(headers->Method() != nullptr);
  }));
}
MockRequestDecoder::~MockRequestDecoder() = default;

MockResponseDecoder::MockResponseDecoder() {
  ON_CALL(*this, decodeHeaders_(_, _))
      .WillByDefault(Invoke(
          [](ResponseHeaderMapPtr& headers, bool) { ASSERT(headers->Status() != nullptr); }));
}
MockResponseDecoder::~MockResponseDecoder() = default;

} // namespace Http
} // namespace Envoy
