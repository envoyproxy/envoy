#pragma once

#include "envoy/decompressor/decompressor.h"

#include "common/common/logger.h"
#include "common/common/zlib/base.h"

#include "zlib.h"

namespace Envoy {
namespace Extensions {
namespace Decompressors {
namespace Gzip {

/**
 * Implementation of decompressor's interface.
 */
class ZlibDecompressor : public Zlib::Base,
                         public Decompressor::Decompressor,
                         public Logger::Loggable<Logger::Id::decompression> {
public:
  ZlibDecompressor();

  /**
   * Constructor that allows setting the size of decompressor's output buffer. It
   * should be called whenever a buffer size different than the 4096 bytes, normally set by the
   * default constructor, is desired. If memory is available and it makes sense to output large
   * chunks of compressed data, zlib documentation suggests buffers sizes on the order of 128K or
   * 256K bytes. @see http://zlib.net/zlib_how.html
   * @param chunk_size amount of memory reserved for the decompressor output.
   */
  ZlibDecompressor(uint64_t chunk_size);

  /**
   * Init must be called in order to initialize the decompressor. Once decompressor is initialized,
   * it cannot be initialized again. Init should run before decompressing any data.
   * @param window_bits sets the size of the history buffer. It must be greater than or equal to
   * the window_bits value provided when data was compressed (zlib manual).
   */
  void init(int64_t window_bits);

  // Decompressor::Decompressor
  void decompress(const Buffer::Instance& input_buffer, Buffer::Instance& output_buffer) override;

  // Flag to track whether error occurred during decompression.
  // When an error occurs, the error code (a negative int) will be stored in this variable.
  int decompression_error_{0};

private:
  bool inflateNext();
};

} // namespace Gzip
} // namespace Decompressors
} // namespace Extensions
} // namespace Envoy
