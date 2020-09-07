#pragma once

#include "envoy/compression/compressor/compressor.h"

#include "brotli/encode.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Brotli {
namespace Compressor {

/**
 * Implementation of compressor's interface.
 */
class BrotliCompressorImpl : public Envoy::Compression::Compressor::Compressor, NonCopyable {
public:
  /**
   * Enum values are used for setting the encoder mode.
   * Generic: in this mode compressor does not know anything in advance about the properties of the
   * input;
   * Text: compression mode for UTF-8 formatted text input;
   * Font: compression mode used in `WOFF` 2.0;
   * Default: compression mode used by `Broli` encoder by default.
   * @see BROTLI_DEFAULT_MODE in brotli manual.
   */
  enum class EncoderMode : uint32_t {
    Generic = BROTLI_MODE_GENERIC,
    Text = BROTLI_MODE_TEXT,
    Font = BROTLI_MODE_FONT,
    Default = BROTLI_DEFAULT_MODE,
  };

  /**
   * Constructor that allows setting the size of compressor's output buffer. It
   * should be called whenever a buffer size different than the 4096 bytes, normally set by the
   * default constructor, is desired.
   * @param quality sets compression level. The higher the quality, the slower the
   * compression. @see BROTLI_PARAM_QUALITY (brotli manual).
   * @param window_bits sets recommended sliding `LZ77` window size.
   * @param input_block_bits sets recommended input block size. Bigger input block size allows
   * better compression, but consumes more memory.
   * @param disable_literal_context_modeling affects usage of "literal context modeling" format
   * feature. This flag is a "decoding-speed vs compression ratio" trade-off.
   * @param mode tunes encoder for specific input. @see EncoderMode enum.
   * @param chunk_size amount of memory reserved for the compressor output.
   */
  BrotliCompressorImpl(const uint32_t quality, const uint32_t window_bits,
                       const uint32_t input_block_bits, const bool disable_literal_context_modeling,
                       const EncoderMode mode, const uint32_t chunk_size);

  // Compression::Compressor::Compressor
  void compress(Buffer::Instance& buffer, Envoy::Compression::Compressor::State state) override;

private:
  struct BrotliContext {
    BrotliContext(uint32_t chunk_size)
        : chunk_ptr(new uint8_t[chunk_size]), next_in(nullptr), next_out(nullptr), avail_in(0),
          avail_out(chunk_size) {
      next_out = chunk_ptr.get();
    }

    std::unique_ptr<uint8_t[]> chunk_ptr;
    const uint8_t* next_in;
    uint8_t* next_out;
    size_t avail_in;
    size_t avail_out;
  };

  void process(BrotliContext& ctx, Buffer::Instance& output_buffer,
               const BrotliEncoderOperation op);

  const size_t chunk_size_;
  std::unique_ptr<BrotliEncoderState, decltype(&BrotliEncoderDestroyInstance)> state_;
};

} // namespace Compressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
