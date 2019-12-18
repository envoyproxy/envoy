#include <chrono>
#include <memory>

#include "envoy/http/filter.h"

#include "test/fuzz/common.pb.validate.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

// Base class for fuzzing a decoder filter.
class DecoderFuzzFilter {
public:
  void fuzz(const test::fuzz::HttpData& data) {
    Http::TestHeaderMapImpl headers = Fuzz::fromHeaders(data.headers());
    // TODO: add end_stream handling and decodeTrailers
    filter_->decodeHeaders(headers, false);
    Buffer::OwnedImpl buffer(data.data());
    filter_->decodeData(buffer, true);
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  std::unique_ptr<Http::StreamDecoderFilter> filter_;
};

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
