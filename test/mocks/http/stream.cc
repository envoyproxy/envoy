#include "test/mocks/http/stream.h"

using testing::_;
using testing::Invoke;
using testing::ReturnRef;

namespace Envoy {
namespace Http {

MockStream::MockStream() : connection_info_provider_(nullptr, nullptr) {
  ON_CALL(*this, addCallbacks(_)).WillByDefault(Invoke([this](StreamCallbacks& callbacks) -> void {
    callbacks_.push_back(&callbacks);
  }));

  ON_CALL(*this, removeCallbacks(_))
      .WillByDefault(Invoke([this](StreamCallbacks& callbacks) -> void {
        for (auto& callback : callbacks_) {
          if (callback == &callbacks) {
            callback = nullptr;
            return;
          }
        }
      }));

  ON_CALL(*this, resetStream(_)).WillByDefault(Invoke([this](StreamResetReason reason) -> void {
    for (auto& callback : callbacks_) {
      if (callback) {
        callback->onResetStream(reason, absl::string_view());
        callback = nullptr;
      }
    }
  }));

  ON_CALL(*this, connectionInfoProvider()).WillByDefault(ReturnRef(connection_info_provider_));

  ON_CALL(*this, setAccount(_))
      .WillByDefault(Invoke(
          [this](Buffer::BufferMemoryAccountSharedPtr account) -> void { account_ = account; }));

  ON_CALL(*this, registerCodecEventCallbacks(_))
      .WillByDefault(Invoke([this](CodecEventCallbacks* codec_callbacks) -> CodecEventCallbacks* {
        std::swap(codec_callbacks, codec_callbacks_);
        return codec_callbacks;
      }));
}

MockStream::~MockStream() {
  if (account_) {
    account_->clearDownstream();
  }
}

} // namespace Http
} // namespace Envoy
