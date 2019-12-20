#include <chrono>
#include <memory>

#include "envoy/http/filter.h"

#include "test/fuzz/common.pb.validate.h"
#include "test/fuzz/utility.h"
#include "test/mocks/http/mocks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {

// Base class for fuzzing a decoder filter.
class DecoderFuzzFilter {
public:
  DecoderFuzzFilter(Http::StreamDecoderFilter* filter) : filter_(filter) {
    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  void fuzz(const test::fuzz::HttpData& data) {
    bool end_stream = false;

    // decodeHeaders and end_stream if no data or trailers is set.
    Http::TestHeaderMapImpl headers = Fuzz::fromHeaders(data.headers());
    if (data.data().size() == 0 && !data.has_trailers()) {
      end_stream = true;
    }
    filter_->decodeHeaders(headers, end_stream);

    // decodeData for each of the data strings.
    for (int i = 0; i < data.data().size(); i++) {
      if (i == data.data().size() - 1 && !data.has_trailers()) {
        end_stream = true;
      }
      Buffer::OwnedImpl buffer(data.data().Get(i));
      filter_->decodeData(buffer, end_stream);
    }

    // decodeTrailers if they are set.
    if (data.has_trailers()) {
      Http::TestHeaderMapImpl trailers = Fuzz::fromHeaders(data.trailers());
      filter_->decodeTrailers(trailers);
    }
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::StreamDecoderFilter* filter_;
};

} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
