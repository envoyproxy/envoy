#include "common/http/filter/gzip_filter.h"
#include "common/common/assert.h"
#include "zlib.h"

#include <iostream>

namespace Envoy {
namespace Http {

GzipFilter::GzipFilter() {}

GzipFilter::~GzipFilter() {}

void GzipFilter::onDestroy() {}

FilterHeadersStatus GzipFilter::decodeHeaders(HeaderMap& headers, bool) {
  Http::HeaderEntry* content_encoding_header = headers.AcceptEncoding();
  if (content_encoding_header &&
      content_encoding_header->value().find(
          Http::Headers::get().AcceptEncodingValues.Deflate.c_str())) {
    deflate_ = true;
  }
  return FilterHeadersStatus::Continue;
}

FilterDataStatus GzipFilter::decodeData(Buffer::Instance&, bool) {
  return FilterDataStatus::Continue;
}

FilterTrailersStatus GzipFilter::decodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

void GzipFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

FilterHeadersStatus GzipFilter::encodeHeaders(HeaderMap& headers, bool end_stream) {
  content_encoding_header_ = headers.ContentEncoding();

  if (!deflate_ || end_stream) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (content_encoding_header_ &&
      content_encoding_header_->value().find(
          Http::Headers::get().ContentEncodingValues.Br.c_str())) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (content_encoding_header_ &&
      content_encoding_header_->value().find(
          Http::Headers::get().ContentEncodingValues.Compress.c_str())) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (content_encoding_header_ &&
      content_encoding_header_->value().find(
          Http::Headers::get().ContentEncodingValues.Deflate.c_str())) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (content_encoding_header_ &&
      content_encoding_header_->value().find(
          Http::Headers::get().ContentEncodingValues.Gzip.c_str())) {
    return Http::FilterHeadersStatus::Continue;
  }

  if (content_encoding_header_ &&
      content_encoding_header_->value().find(
          Http::Headers::get().ContentEncodingValues.Gzip.c_str())) {
    return Http::FilterHeadersStatus::Continue;
  }
  
  is_compressed_ = false;

  headers.removeContentLength();
  headers.insertContentEncoding().value(Http::Headers::get().ContentEncodingValues.Deflate);
  headers.removeEtag();

  return Http::FilterHeadersStatus::Continue;
}

FilterDataStatus GzipFilter::encodeData(Buffer::Instance& data, bool end_stream) {
  if (!deflate_ || is_compressed_) {
    return Http::FilterDataStatus::Continue;
  }

  if (end_stream) {
    std::cout << "total bytes in: " << total_bytes_in_ << std::endl;
    std::cout << "total bytes out: " << total_bytes_out_ << std::endl;
    return Http::FilterDataStatus::Continue;
  }

  if (compress(data)) {
    return Http::FilterDataStatus::Continue;
  } else {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
}

FilterTrailersStatus GzipFilter::encodeTrailers(HeaderMap&) {
  return FilterTrailersStatus::Continue;
}

void GzipFilter::setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) {
  encoder_callbacks_ = &callbacks;
}

bool GzipFilter::compress(Buffer::Instance& data) {
  Buffer::InstancePtr temp_buffer{new Buffer::OwnedImpl()};
  std::unique_ptr<Buffer::RawSlice> in(new Buffer::RawSlice);
  std::unique_ptr<Buffer::RawSlice> out(new Buffer::RawSlice);

  int result, num_slices, data_len, flush_state, num_bytes_read, num_bytes_write;

  std::unique_ptr<z_stream> zstream(new z_stream());
  zstream->zalloc = Z_NULL;
  zstream->zfree = Z_NULL;
  zstream->opaque = Z_NULL;

  result = deflateInit(zstream.get(), Z_BEST_COMPRESSION);
  if (result != Z_OK) {
    return false;
  }

  while (data.length() > 0) {
    num_slices = data.getRawSlices(in.get(), 1);
    if (num_slices) {
      zstream->avail_in = in->len_;
      zstream->next_in = static_cast<Bytef*>(in->mem_);
    }

    data_len = data.length();
    if (data_len - zstream->avail_in == 0) {
      flush_state = Z_FINISH;
    } else {
      flush_state = Z_SYNC_FLUSH;
    }

    temp_buffer->reserve(data_len, out.get(), 1);
    zstream->avail_out = out->len_;
    zstream->next_out = static_cast<unsigned char*>(out->mem_);

    result = deflate(zstream.get(), flush_state);
    if (result != Z_OK && result != Z_STREAM_END) {
      return false;
    }

    num_bytes_read = in->len_ - zstream->avail_in;
    num_bytes_write = out->len_ - zstream->avail_out;
    data.drain(num_bytes_read);
    out->len_ = num_bytes_write;
    temp_buffer->commit(out.get(), 1);
  };

  total_bytes_in_ += zstream->total_in;
  total_bytes_out_ += zstream->total_out;

  result = deflateEnd(zstream.get());
  if (result != Z_OK) {
    return false;
  }

  data.move(*temp_buffer);
  return true;
}

} // Http
} // Envoy