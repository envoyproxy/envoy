#include "test/mocks/http/stream.h"

using testing::_;
using testing::Invoke;

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
}

MockStream::~MockStream() {}

} // namespace Http
} // namespace Envoy
