#include "source/extensions/filters/http/connect_stats/response_frame_counter.h"

#include "source/common/json/json_loader.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectStats {

uint64_t ConnectResponseFrameCounter::inspect(const Buffer::Instance& input) {
  bool was_at_status_frame = status_frame_ != nullptr;

  uint64_t delta = Grpc::FrameInspector::inspect(input);

  if (!was_at_status_frame && status_frame_ != nullptr) {
    delta--;
    count_--;
  }

  return delta;
}

bool ConnectResponseFrameCounter::frameStart(uint8_t flags) {
  if ((flags & Grpc::CONNECT_FH_EOS) != 0) {
    status_frame_ = std::make_unique<Buffer::OwnedImpl>();
  }
  return true;
}

void ConnectResponseFrameCounter::frameData(uint8_t* data, uint64_t size) {
  if (status_frame_) {
    status_frame_->add(data, size);
  }
}

void ConnectResponseFrameCounter::frameDataEnd() {
  if (!status_frame_) {
    return;
  }

  try {
    Json::ObjectSharedPtr json_body = Json::Factory::loadFromString(status_frame_->toString());
    if (auto error = json_body->getObject("error", true); error && !error->empty()) {
      status_code_ = error->getString("code", "");
    } else {
      status_code_ = "";
    }
  } catch (Json::Exception&) {
    status_code_ = "";
  }
}

} // namespace ConnectStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
