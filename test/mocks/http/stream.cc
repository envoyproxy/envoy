#include "test/mocks/http/stream.h"

using testing::_;
using testing::Invoke;
using testing::ReturnRef;

namespace Envoy {
namespace Http {

MockStream::MockStream() {
  ON_CALL(*this, addCallbacks(_)).WillByDefault(Invoke([this](StreamCallbacks& callbacks) -> void {
    callbacks_.push_back(&callbacks);
  }));

  ON_CALL(*this, removeCallbacks(_))
      .WillByDefault(
          Invoke([this](StreamCallbacks& callbacks) -> void { callbacks_.remove(&callbacks); }));

  ON_CALL(*this, resetStream(_)).WillByDefault(Invoke([this](StreamResetReason reason) -> void {
    for (StreamCallbacks* callbacks : callbacks_) {
      callbacks->onResetStream(reason, absl::string_view());
    }
  }));

  ON_CALL(*this, connectionLocalAddress()).WillByDefault(ReturnRef(connection_local_address_));
}

MockStream::~MockStream() = default;

} // namespace Http
} // namespace Envoy
