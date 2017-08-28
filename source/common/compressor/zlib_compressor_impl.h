#pragma once

#include "envoy/compressor/compressor.h"

#include "zlib.h"

namespace Envoy {
namespace Compressor {

class ZlibCompressorImpl : public Compressor {

public:
  enum CompressionLevel {
    zero = Z_NO_COMPRESSION,
    default_compression = Z_DEFAULT_COMPRESSION,
    best = Z_BEST_COMPRESSION,
    speed = Z_BEST_SPEED,
  };

  enum CompressionStrategy {
    filtered = Z_FILTERED,
    huffman = Z_HUFFMAN_ONLY,
    rle = Z_RLE,
    fixed = Z_FIXED,
    default_strategy = Z_DEFAULT_STRATEGY,
  };

  ZlibCompressorImpl();
  ~ZlibCompressorImpl();

  /**
   * Buffer size for feeding data to and pulling data from the zlib routines.
   * For this implementation, it should never be less than the length of evbuffer
   * passsed into ZlibCompressorImpl::start(Buffer::Instance& in, Buffer::Instance& out).
   * @param chunk available memory to deflate data, default = 4096.
   */
  void setChunk(uint64_t chunk);

  /**
   * Used for compression initialization
   * @param level compression level applied, default = Z_DEFAULT_COMPRESSION.
   * @param strategy compression strategy applied, default = Z_DEFAULT_STRATEGY.
   * @param window_bits
   * @param memory_level
   * @return bool true if initialization succceeded or false if an error occurred
   */
  bool init(CompressionLevel level, CompressionStrategy strategy, int window_bits,
            uint memory_level);

  /**
   * Used for decompression initialization
   * @param window_bits
   * @return bool true if initialization succceeded or false if an error occurred
   */
  bool init(int window_bits = 0);

  /**
   * Finalize the compression engine and free all dynamic memory allocated during
   * compression. If this function does not get called, distructor will take care
   * of the memmory by calling deflateEnd(). Calling this method though allows to
   * inspect the compressor has errors.
   * @return bool if compressor finalized without error or false otherwise.
   */
  bool finish();

  bool compress(const Buffer::Instance& in, Buffer::Instance& out) override;

  bool decompress(const Buffer::Instance& in, Buffer::Instance& out) override;

private:
  bool process(const Buffer::Instance& in, Buffer::Instance& out, int (*fptr)(z_stream*, int));

  bool is_deflate_{true};
  uint64_t chunk_{4096};

  // struct ZstreamDeleter {
  //   void operator()(z_stream* z) const {
  //     if (is_deflate_) {
  //       deflateEnd(z);
  //     } else {
  //       inflateEnd(z);
  //     }
  //     delete z;
  //   }
  // };

  std::unique_ptr<z_stream> zlib_ptr_{nullptr};
};

} // namespace Compressor
} // namespace Envoy