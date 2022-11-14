#pragma once

#include "source/common/buffer/buffer_impl.h"
#include "source/common/grpc/codec.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectStats {

// An adaptation of the gRPC frame inspector that handles the Buf Connect end-of-stream frame.
class ConnectResponseFrameCounter : protected Grpc::FrameInspector {
public:
  uint64_t inspect(const Buffer::Instance& input);

  absl::string_view statusCode() const { return status_code_.value_or(""); }

  bool endStream() const { return status_frame_ != nullptr && status_code_.has_value(); }

  uint64_t frameCount() const { return Grpc::FrameInspector::frameCount(); }

protected:
  bool frameStart(uint8_t flags) override;
  void frameData(uint8_t* data, uint64_t size) override;
  void frameDataEnd() override;

  std::unique_ptr<Buffer::OwnedImpl> status_frame_;
  absl::optional<absl::string_view> status_code_;
};

} // namespace ConnectStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
