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

DEFINE_PROTO_FUZZER(
    const test::extensions::filters::http::buffer::BufferFilterFuzzTestCase& input) {
  BufferFilterConfigSharedPtr config(std::make_shared<BufferFilterConfig>(input.config()));
  BufferFilter filter(config);
  DecoderFuzzFilter decoder_filter(&filter);
  // decoder_filter.fuzz(input.data());
}

} // namespace BufferFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
