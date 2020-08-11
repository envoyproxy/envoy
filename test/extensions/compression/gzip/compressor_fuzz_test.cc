#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/stats/isolated_store_impl.h"

#include "extensions/compression/gzip/compressor/zlib_compressor_impl.h"
#include "extensions/compression/gzip/decompressor/zlib_decompressor_impl.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Gzip {
namespace Compressor {
namespace Fuzz {

// Fuzzer for zlib compression. While the zlib project has its own fuzzer, this
// fuzzer validates that the Envoy wiring around zlib makes sense and the
// specific ways we configure it are safe. The fuzzer below validates a round
// trip compress-decompress pair; the decompressor itself is not fuzzed beyond
// whatever the compressor emits, as it exists only as a test utility today.
DEFINE_FUZZER(const uint8_t* buf, size_t len) {

  FuzzedDataProvider provider(buf, len);
  ZlibCompressorImpl compressor;
  Stats::IsolatedStoreImpl stats_store;
  Decompressor::ZlibDecompressorImpl decompressor{stats_store, "test"};

  // Select target compression level. We can't use ConsumeEnum() since the range
  // is non-contiguous.
  const ZlibCompressorImpl::CompressionLevel compression_levels[] = {
      ZlibCompressorImpl::CompressionLevel::Best,
      ZlibCompressorImpl::CompressionLevel::Speed,
      ZlibCompressorImpl::CompressionLevel::Standard,
  };
  const ZlibCompressorImpl::CompressionLevel target_compression_level =
      provider.PickValueInArray(compression_levels);

  // Select target compression strategy. We can't use ConsumeEnum() since the
  // range does not start with zero.
  const ZlibCompressorImpl::CompressionStrategy compression_strategies[] = {
      ZlibCompressorImpl::CompressionStrategy::Filtered,
      ZlibCompressorImpl::CompressionStrategy::Huffman,
      ZlibCompressorImpl::CompressionStrategy::Rle,
      ZlibCompressorImpl::CompressionStrategy::Standard,
  };
  const ZlibCompressorImpl::CompressionStrategy target_compression_strategy =
      provider.PickValueInArray(compression_strategies);

  // Select target window bits. The range comes from the PGV constraints in
  // api/envoy/config/filter/http/gzip/v2/gzip.proto.
  const int64_t target_window_bits = provider.ConsumeIntegralInRange(9, 15);

  // Select memory level. The range comes from the restriction in the init()
  // header comments.
  const uint64_t target_memory_level = provider.ConsumeIntegralInRange(1, 9);

  compressor.init(target_compression_level, target_compression_strategy, target_window_bits,
                  target_memory_level);
  decompressor.init(target_window_bits);

  bool provider_empty = provider.remaining_bytes() == 0;
  Buffer::OwnedImpl full_input;
  Buffer::OwnedImpl full_output;
  while (!provider_empty) {
    const std::string next_data = provider.ConsumeRandomLengthString(provider.remaining_bytes());
    ENVOY_LOG_MISC(debug, "Processing {} bytes", next_data.size());
    full_input.add(next_data);
    Buffer::OwnedImpl buffer{next_data.data(), next_data.size()};
    provider_empty = provider.remaining_bytes() == 0;
    compressor.compress(buffer, provider_empty ? Envoy::Compression::Compressor::State::Finish
                                               : Envoy::Compression::Compressor::State::Flush);
    decompressor.decompress(buffer, full_output);
  }
  RELEASE_ASSERT(full_input.toString() == full_output.toString(), "");
  RELEASE_ASSERT(compressor.checksum() == decompressor.checksum(), "");
}

} // namespace Fuzz
} // namespace Compressor
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
