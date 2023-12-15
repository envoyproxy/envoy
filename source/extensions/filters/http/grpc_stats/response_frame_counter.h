#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/codec.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {

// An adaptation of the gRPC frame inspector that handles the Connect end-of-stream frame.
class ResponseFrameCounter : protected Grpc::FrameInspector {
public:
  uint64_t inspect(const Buffer::Instance& input);

  uint64_t frameCount() const { return Grpc::FrameInspector::frameCount(); }

  bool connectSuccess() const {
    return connect_state_ == ConnectEndFrameState::Parsed && !connect_error_code_.has_value();
  }

protected:
  enum class ConnectEndFrameState {
    None,
    Buffering,
    Parsed,
    Invalid,
  };

  bool frameStart(uint8_t flags) override;
  void frameData(uint8_t* data, uint64_t size) override;
  void frameDataEnd() override;

  ConnectEndFrameState connect_state_{ConnectEndFrameState::None};
  std::unique_ptr<Buffer::OwnedImpl> connect_eos_buffer_;
  absl::optional<absl::string_view> connect_error_code_;
};

} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
