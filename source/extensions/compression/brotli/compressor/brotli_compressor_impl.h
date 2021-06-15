#pragma once

#include "envoy/compression/compressor/compressor.h"

#include "source/extensions/compression/brotli/common/base.h"

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
   * Generic: in this mode the compressor does not know anything in advance about the properties of
   * the input;
   * Text: compression mode for UTF-8 formatted text input;
   * Font: compression mode used in `WOFF` 2.0;
   * Default: compression mode used by brotli encoder by default which is Generic currently.
   * @see BROTLI_DEFAULT_MODE in brotli manual.
   */
  enum class EncoderMode : uint32_t {
    Generic = BROTLI_MODE_GENERIC,
    Text = BROTLI_MODE_TEXT,
    Font = BROTLI_MODE_FONT,
    Default = BROTLI_DEFAULT_MODE,
  };

  /**
   * Constructor.
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
  void process(Common::BrotliContext& ctx, Buffer::Instance& output_buffer,
               const BrotliEncoderOperation op);

  const uint32_t chunk_size_;
  std::unique_ptr<BrotliEncoderState, decltype(&BrotliEncoderDestroyInstance)> state_;
};

} // namespace Compressor
} // namespace Brotli
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
