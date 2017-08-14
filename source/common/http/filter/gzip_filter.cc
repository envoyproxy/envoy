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
  if (end_stream) {
    return Http::FilterDataStatus::Continue;
  }
  
  if (!deflate_ || is_compressed_) {
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
  Buffer::InstancePtr buffer{new Buffer::OwnedImpl()};
  Buffer::RawSlice in_slice{};
  Buffer::RawSlice out_slice{};

  std::unique_ptr<z_stream> ZstreamPtr{new z_stream()};
  ZstreamPtr->zalloc = Z_NULL;
  ZstreamPtr->zfree = Z_NULL;
  ZstreamPtr->opaque = Z_NULL;

  if (deflateInit(ZstreamPtr.get(), Z_BEST_COMPRESSION) != Z_OK) {
    return false;
  }

  uint data_len{};
  uint num_bytes_read{};
  uint num_bytes_write{};
  uint num_slices{};
  uint flush_state{};
  uint result{};

  while (data.length() > 0) {
    num_slices = data.getRawSlices(&in_slice, 1);
    if (num_slices) {
      ZstreamPtr->avail_in = in_slice.len_;
      ZstreamPtr->next_in = static_cast<Bytef *>(in_slice.mem_);
    }

    data_len = data.length();
    if (data_len - ZstreamPtr->avail_in == 0) {
      flush_state = Z_FINISH;
    } else {
      flush_state = Z_SYNC_FLUSH;
    }

    buffer->reserve(data_len, &out_slice, 1);
    ZstreamPtr->avail_out = out_slice.len_;
    ZstreamPtr->next_out = static_cast<Bytef *>(out_slice.mem_);

    result = deflate(ZstreamPtr.get(), flush_state);
    if (result != Z_OK && result != Z_STREAM_END) {
      return false;
    }

    num_bytes_read = in_slice.len_ - ZstreamPtr->avail_in;
    num_bytes_write = out_slice.len_ - ZstreamPtr->avail_out;
    data.drain(num_bytes_read);
    out_slice.len_ = num_bytes_write;
    buffer->commit(&out_slice, 1);
  };

  if (deflateEnd(ZstreamPtr.get()) != Z_OK) {
    return false;
  }
  
  data.move(*buffer);
  return true;
}

} // Http
} // Envoy
