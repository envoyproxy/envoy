#include "contrib/qat/compression/qatzip/compressor/source/qatzip_compressor_impl.h"

#include <memory>

#include "source/common/common/assert.h"

namespace Envoy {
namespace Extensions {
namespace Compression {
namespace Qatzip {
namespace Compressor {

QatzipCompressorImpl::QatzipCompressorImpl(QzSession_T* session)
    : QatzipCompressorImpl(session, 4096) {}

// TODO(rojkov): add lower limit to chunk_size in proto definition.
QatzipCompressorImpl::QatzipCompressorImpl(QzSession_T* session, size_t chunk_size)
    : chunk_size_{chunk_size}, avail_in_{0}, avail_out_{chunk_size - 10},
      chunk_char_ptr_(new unsigned char[chunk_size]), session_{session}, stream_{}, input_len_(0) {
  RELEASE_ASSERT(session_ != nullptr,
                 "QATzip compressor must be created with non-null QATzip session");
  static unsigned char gzheader[10] = {0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 3};
  stream_.out = static_cast<unsigned char*>(mempcpy(chunk_char_ptr_.get(), gzheader, 10));
}

QatzipCompressorImpl::~QatzipCompressorImpl() { qzEndStream(session_, &stream_); }

void QatzipCompressorImpl::compress(Buffer::Instance& buffer,
                                    Envoy::Compression::Compressor::State state) {

  for (const Buffer::RawSlice& input_slice : buffer.getRawSlices()) {
    avail_in_ = input_slice.len_;
    stream_.in = static_cast<unsigned char*>(input_slice.mem_);

    while (avail_in_ > 0) {
      process(buffer, 0);
    }

    buffer.drain(input_slice.len_);
  }

  if (state == Envoy::Compression::Compressor::State::Finish) {
    do {
      process(buffer, 1);
    } while (stream_.pending_out > 0);

    const size_t n_output = chunk_size_ - avail_out_;
    if (n_output > 0) {
      buffer.add(static_cast<void*>(chunk_char_ptr_.get()), n_output);
    }
    buffer.writeLEInt<uint32_t>(stream_.crc_32);
    buffer.writeLEInt<uint32_t>(input_len_);
  }
}

void QatzipCompressorImpl::process(Buffer::Instance& output_buffer, unsigned int last) {
  stream_.in_sz = avail_in_;
  stream_.out_sz = avail_out_;
  auto status = qzCompressStream(session_, &stream_, last);
  // NOTE: stream_.in_sz and stream_.out_sz have changed their semantics after the call
  //       to qzCompressStream(). Despite their name the new values are consumed input
  //       and produced output (not available buffer sizes).
  avail_out_ -= stream_.out_sz;
  avail_in_ -= stream_.in_sz;
  input_len_ += stream_.in_sz;
  stream_.in = stream_.in + stream_.in_sz;
  stream_.out = stream_.out + stream_.out_sz;
  RELEASE_ASSERT(status == QZ_OK, "");
  if (avail_out_ == 0) {
    // The chunk is full, so copy it to the output buffer and reset context.
    output_buffer.add(static_cast<void*>(chunk_char_ptr_.get()), chunk_size_);
    chunk_char_ptr_ = std::make_unique<unsigned char[]>(chunk_size_);
    avail_out_ = chunk_size_;
    stream_.out = chunk_char_ptr_.get();
  }
}

} // namespace Compressor
} // namespace Qatzip
} // namespace Compression
} // namespace Extensions
} // namespace Envoy
