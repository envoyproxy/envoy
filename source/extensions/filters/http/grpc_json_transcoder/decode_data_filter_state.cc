#include "source/extensions/filters/http/grpc_json_transcoder/decode_data_filter_state.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

HttpBackendDataFilterState::HttpBackendDataFilterState(Envoy::Buffer::Instance& buffer) {
  data.move(buffer);
}

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
