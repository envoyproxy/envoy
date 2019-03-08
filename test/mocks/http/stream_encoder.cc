#include "test/mocks/http/stream_encoder.h"

namespace Envoy {
namespace Http {

MockStreamEncoder::MockStreamEncoder() {
  ON_CALL(*this, getStream()).WillByDefault(ReturnRef(stream_));
}

MockStreamEncoder::~MockStreamEncoder() {}

} // namespace Http
} // namespace Envoy
