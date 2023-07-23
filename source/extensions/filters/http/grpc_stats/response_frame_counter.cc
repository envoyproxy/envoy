#include "source/extensions/filters/http/grpc_stats/response_frame_counter.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {

uint64_t ResponseFrameCounter::inspect(const Buffer::Instance& input) {
  bool was_at_status_frame = connect_state_ != ConnectEndFrameState::None;

  uint64_t delta = Grpc::FrameInspector::inspect(input);

  // Offset to account for the Connect end-of-stream frame.
  if (connect_state_ != ConnectEndFrameState::None && !was_at_status_frame) {
    delta--;
    count_--;
  }

  return delta;
}

bool ResponseFrameCounter::frameStart(uint8_t flags) {
  // There should be no frames after the connect response frame.
  if (connect_state_ != ConnectEndFrameState::None) {
    return false;
  }

  if ((flags & Grpc::CONNECT_FH_EOS) != 0) {
    ASSERT(connect_eos_buffer_ == nullptr);

    connect_state_ = ConnectEndFrameState::Buffering;
    connect_eos_buffer_ = std::make_unique<Buffer::OwnedImpl>();
  }
  return true;
}

void ResponseFrameCounter::frameData(uint8_t* data, uint64_t size) {
  if (connect_state_ == ConnectEndFrameState::Buffering) {
    ASSERT(connect_eos_buffer_ != nullptr);

    connect_eos_buffer_->add(data, size);
  }
}

void ResponseFrameCounter::frameDataEnd() {
  if (connect_state_ != ConnectEndFrameState::Buffering) {
    return;
  }

  ASSERT(connect_eos_buffer_ != nullptr);

  bool has_unknown_field;
  ProtobufWkt::Struct message;
  auto status =
      MessageUtil::loadFromJsonNoThrow(connect_eos_buffer_->toString(), message, has_unknown_field);
  if (!has_unknown_field && !status.ok()) {
    connect_state_ = ConnectEndFrameState::Invalid;
    return;
  }

  connect_state_ = ConnectEndFrameState::Parsed;

  const auto& error_field = message.fields().find("error");
  if (error_field == message.fields().end()) {
    return;
  }

  connect_error_code_ = "";

  const auto& error_struct = error_field->second.struct_value();
  const auto& code_field = error_struct.fields().find("code");
  if (code_field == error_struct.fields().end()) {
    return;
  }

  connect_error_code_ = code_field->second.string_value();
}

} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
