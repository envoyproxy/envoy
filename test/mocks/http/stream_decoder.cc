#include "test/mocks/http/stream_decoder.h"

using testing::_;
using testing::Invoke;

namespace Envoy {
namespace Http {

MockRequestDecoder::MockRequestDecoder() {
  ON_CALL(*this, decodeHeaders_(_, _)).WillByDefault(Invoke([](RequestHeaderMapPtr& headers, bool) {
    // Check to see that method is not-null. Path can be null for CONNECT and authority can be null
    // at the codec level.
    ASSERT_NE(nullptr, headers->Method());
  }));
}
MockRequestDecoder::~MockRequestDecoder() = default;

MockResponseDecoder::MockResponseDecoder() {
  ON_CALL(*this, decodeHeaders_(_, _))
      .WillByDefault(Invoke(
          [](ResponseHeaderMapPtr& headers, bool) { ASSERT_NE(nullptr, headers->Status()); }));
}
MockResponseDecoder::~MockResponseDecoder() = default;

} // namespace Http
} // namespace Envoy
