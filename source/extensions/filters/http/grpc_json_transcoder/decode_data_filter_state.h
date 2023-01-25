#pragma once

#include "source/common/stream_info/filter_state_impl.h"
#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

constexpr absl::string_view kHeaderOnlyDecodeDataKey =
    "envoy.extensions.httpfilters.GrpcJsonTranscoder."
    "HeaderOnlyDecodeDataKey";

struct HttpBackendDataFilterState
    : public Envoy::StreamInfo::FilterState::Object {
 public:
  explicit HttpBackendDataFilterState(Envoy::Buffer::Instance& buffer);

  Envoy::Buffer::OwnedImpl data;
};

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
