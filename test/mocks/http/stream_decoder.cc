#include "test/mocks/http/stream_decoder.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Http {

MockRequestDecoderHandle::MockRequestDecoderHandle() {
  ON_CALL(*this, get()).WillByDefault(Return(OptRef<RequestDecoder>()));
}

MockRequestDecoder::MockRequestDecoder() {
  ON_CALL(*this, decodeHeaders_(_, _))
      .WillByDefault(Invoke([](RequestHeaderMapSharedPtr& headers, bool) {
        // Check to see that method is not-null. Path can be null for CONNECT and authority can be
        // null at the codec level.
        ASSERT_NE(nullptr, headers->Method());
      }));
  ON_CALL(*this, getRequestDecoderHandle()).WillByDefault(Invoke([this]() {
    auto handle = std::make_unique<MockRequestDecoderHandle>();
    ON_CALL(*handle, get()).WillByDefault(Return(OptRef<RequestDecoder>(*this)));
    return handle;
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
