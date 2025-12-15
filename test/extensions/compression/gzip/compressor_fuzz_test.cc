#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/compression/gzip/compressor/zlib_compressor_impl.h"
#include "source/extensions/compression/gzip/decompressor/zlib_decompressor_impl.h"

#include "test/extensions/compression/gzip/compressor_fuzz_input.pb.h"
#include "test/extensions/compression/gzip/compressor_fuzz_input.pb.validate.h"
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
DEFINE_PROTO_FUZZER(
    const envoy::extensions::compression::gzip::compressor::fuzz::CompressorFuzzInput& input) {
  ZlibCompressorImpl compressor;
  Stats::IsolatedStoreImpl stats_store;
  Decompressor::ZlibDecompressorImpl decompressor{*stats_store.rootScope(), "test", 4096, 100};

  ZlibCompressorImpl::CompressionLevel target_compression_level;
  switch (input.compression_level()) {
  case envoy::extensions::compression::gzip::compressor::fuzz::CompressorFuzzInput::BEST:
    target_compression_level = ZlibCompressorImpl::CompressionLevel::Best;
    break;
  case envoy::extensions::compression::gzip::compressor::fuzz::CompressorFuzzInput::SPEED:
    target_compression_level = ZlibCompressorImpl::CompressionLevel::Speed;
    break;
  default:
    target_compression_level = ZlibCompressorImpl::CompressionLevel::Standard;
    break;
  }

  ZlibCompressorImpl::CompressionStrategy target_compression_strategy;
  switch (input.compression_strategy()) {
  case envoy::extensions::compression::gzip::compressor::fuzz::CompressorFuzzInput::FILTERED:
    target_compression_strategy = ZlibCompressorImpl::CompressionStrategy::Filtered;
    break;
  case envoy::extensions::compression::gzip::compressor::fuzz::CompressorFuzzInput::HUFFMAN:
    target_compression_strategy = ZlibCompressorImpl::CompressionStrategy::Huffman;
    break;
  case envoy::extensions::compression::gzip::compressor::fuzz::CompressorFuzzInput::RLE:
    target_compression_strategy = ZlibCompressorImpl::CompressionStrategy::Rle;
    break;
  default:
    target_compression_strategy = ZlibCompressorImpl::CompressionStrategy::Standard;
    break;
  }

  compressor.init(target_compression_level, target_compression_strategy, input.window_bits(),
                  input.memory_level());
  decompressor.init(input.window_bits());

  ENVOY_LOG_MISC(
      debug,
      "Fuzz test parameters: compression_level={} compression_strategy={} window_bits={} "
      "memory_level={}",
      static_cast<int>(target_compression_level), static_cast<int>(target_compression_strategy),
      input.window_bits(), input.memory_level());

  Buffer::OwnedImpl full_input;
  Buffer::OwnedImpl full_output;
  for (int i = 0; i < input.data_chunks_size(); ++i) {
    const std::string& next_data = input.data_chunks(i);
    ENVOY_LOG_MISC(debug, "Processing {} bytes", next_data.size());
    full_input.add(next_data);
    Buffer::OwnedImpl buffer{next_data.data(), next_data.size()};
    const bool last_chunk = i == input.data_chunks_size() - 1;
    compressor.compress(buffer, last_chunk ? Envoy::Compression::Compressor::State::Finish
                                           : Envoy::Compression::Compressor::State::Flush);
    decompressor.decompress(buffer, full_output);
  }
  if (stats_store.counterFromString("test.zlib_data_error").value() == 0) {
    RELEASE_ASSERT(full_input.toString() == full_output.toString(), "");
    RELEASE_ASSERT(compressor.checksum() == decompressor.checksum(), "");
  }
}

} // namespace Fuzz
} // namespace Compressor
} // namespace Gzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
