#include <chrono>
#include <memory>

#include "envoy/config/filter/http/buffer/v2/buffer.pb.h"

#include "extensions/filters/http/buffer/buffer_filter.h"
#include "extensions/filters/http/well_known_names.h"

#include "test/extensions/filters/http/buffer/buffer_filter_fuzz.pb.h"
#include "test/extensions/filters/http/common/fuzz/decoder_filter_fuzz.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/http/mocks.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BufferFilter {

class BufferFilterFuzz : public DecoderFuzzFilter {
public:
  BufferFilterFuzz(envoy::config::filter::http::buffer::v2::Buffer proto_config)
      : config_(std::make_shared<BufferFilterConfig>(proto_config)) {
    // Initialize filter and setup callbacks.
    filter_ = std::make_unique<BufferFilter>(config_);
    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  BufferFilterConfigSharedPtr config_;
};

DEFINE_PROTO_FUZZER(
    const test::extensions::filters::http::buffer::BufferFilterFuzzTestCase& input) {
  BufferFilterFuzz filter(input.config());
  filter.fuzz(input.data());
}

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
