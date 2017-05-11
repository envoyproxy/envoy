#include "common/grpc/grpc_web_filter.h"

#include <arpa/inet.h>

#include "common/common/base64.h"

namespace Grpc {

GrpcWebFilter::GrpcWebFilter() : is_text_request_(false), is_text_response_(false) {}

GrpcWebFilter::~GrpcWebFilter() {}

// Implements StreamDecoderFilter.
Http::FilterHeadersStatus GrpcWebFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type != nullptr &&
      Constants::get().CONTENT_TYPE_GRPC_WEB_TEXT() == content_type->value().c_str()) {
    is_text_request_ = true;
  }
  headers.removeContentType();
  headers.insertContentType().value(Constants::get().CONTENT_TYPE_GRPC());

  const Http::HeaderEntry* accept = headers.get(Http::LowerCaseString("accept"));
  if (accept != nullptr &&
      Constants::get().CONTENT_TYPE_GRPC_WEB_TEXT() == accept->value().c_str()) {
    is_text_response_ = true;
  }
  headers.addStatic(Constants::get().HTTP_KEY_TE(), Constants::get().HTTP_KEY_TE_VALUE());
  headers.addStatic(Constants::get().HTTP_KEY_GRPC_ACCEPT_ENCODING(),
                    Constants::get().HTTP_KEY_GRPC_ACCEPT_ENCODING_VALUE());
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus GrpcWebFilter::decodeData(Buffer::Instance& data, bool) {
  if (!is_text_request_) {
    return Http::FilterDataStatus::Continue;
  }

  // Parse application/grpc-web-text format.
  if (data.length() + decoding_buffer_.length() < 4) {
    decoding_buffer_.move(data);
    data.drain(data.length());
    return Http::FilterDataStatus::Continue;
  }

  uint64_t needed = (data.length() + decoding_buffer_.length()) / 4 * 4 - decoding_buffer_.length();
  decoding_buffer_.move(data, needed);
  std::string decoded = Base64::decode(
      std::string(static_cast<const char*>(decoding_buffer_.linearize(decoding_buffer_.length())),
                  decoding_buffer_.length()));
  decoding_buffer_.drain(decoding_buffer_.length());
  decoding_buffer_.move(data);
  data.add(decoded);
  return Http::FilterDataStatus::Continue;
}

// Implements StreamEncoderFilter.
Http::FilterHeadersStatus GrpcWebFilter::encodeHeaders(Http::HeaderMap& headers, bool) {
  if (is_text_response_) {
    headers.ContentType()->value(Constants::get().CONTENT_TYPE_GRPC_WEB_TEXT());
  } else {
    headers.ContentType()->value(Constants::get().CONTENT_TYPE_GRPC_WEB());
  }
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus GrpcWebFilter::encodeData(Buffer::Instance& data, bool) {
  if (!is_text_response_) {
    return Http::FilterDataStatus::Continue;
  }

  // Encodes the response as base64.
  std::vector<Frame> frames;
  decoder_.decode(data, frames);
  for (auto& frame : frames) {
    Buffer::OwnedImpl temp;
    temp.add(&frame.flags_, 1);
    temp.add(&frame.length_, 4);
    temp.add(*frame.data_);
    data.add(temp);
  }
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus GrpcWebFilter::encodeTrailers(Http::HeaderMap& trailers) {
  if (!encoder_callbacks_->encodingBuffer()) {
    encoder_callbacks_->encodingBuffer().reset(new Buffer::OwnedImpl());
  }
  encoder_callbacks_->encodingBuffer()->add(&Constants::get().GRPC_WEB_TRAILER, 1);
  trailers.iterate([](const Http::HeaderEntry& header, void* context) -> void {
    Buffer::Instance& temp = static_cast<GrpcWebFilter*>(context)->encoding_buffer_trailers_;
    temp.add(header.key().c_str(), header.key().size());
    temp.add(":");
    temp.add(header.value().c_str(), header.value().size());
    temp.add("\r\n");
  }, this);
  uint64_t length = htonl(encoding_buffer_trailers_.length());
  encoder_callbacks_->encodingBuffer()->add(&length, 4);
  encoder_callbacks_->encodingBuffer()->move(encoding_buffer_trailers_);
  return Http::FilterTrailersStatus::Continue;
}
} // namespace Grpc
