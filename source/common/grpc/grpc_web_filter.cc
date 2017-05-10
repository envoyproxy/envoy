#include "common/grpc/grpc_web_filter.h"

#include "common/common/base64.h"

namespace Grpc {

const std::string GrpcWebFilter::GRPC_WEB_CONTENT_TYPE{"application/grpc-web"};
const std::string GrpcWebFilter::GRPC_WEB_TEXT_CONTENT_TYPE{"application/grpc-web-text"};
const std::string GrpcWebFilter::GRPC_CONTENT_TYPE{"application/grpc"};
const uint8_t GrpcWebFilter::GRPC_WEB_TRAILER = 0b10000000;
const Http::LowerCaseString GrpcWebFilter::HTTP_TE_KEY{"te"};
const std::string GrpcWebFilter::HTTP_TE_VALUE{"trailers"};
const Http::LowerCaseString GrpcWebFilter::GRPC_ACCEPT_ENCODING_KEY{"grpc-accept-encoding"};
const std::string GrpcWebFilter::GRPC_ACCEPT_ENCODING_VALUE{"identity,deflate,gzip"};

GrpcWebFilter::GrpcWebFilter() : is_text_request_(false), is_text_response_(false) {}

GrpcWebFilter::~GrpcWebFilter() {}

// Implements StreamDecoderFilter.
Http::FilterHeadersStatus GrpcWebFilter::decodeHeaders(Http::HeaderMap& headers, bool) {
  const Http::HeaderEntry* content_type = headers.ContentType();
  if (content_type != nullptr && GRPC_WEB_TEXT_CONTENT_TYPE == content_type->value().c_str()) {
    is_text_request_ = true;
  }
  headers.removeContentType();
  headers.insertContentType().value(GRPC_CONTENT_TYPE);

  const Http::HeaderEntry* accept = headers.get(Http::LowerCaseString("accept"));
  if (accept != nullptr && GRPC_WEB_TEXT_CONTENT_TYPE == accept->value().c_str()) {
    is_text_response_ = true;
  }
  headers.addStatic(HTTP_TE_KEY, HTTP_TE_VALUE);
  headers.addStatic(GRPC_ACCEPT_ENCODING_KEY, GRPC_ACCEPT_ENCODING_VALUE);
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
    headers.ContentType()->value(GRPC_WEB_TEXT_CONTENT_TYPE);
  } else {
    headers.ContentType()->value(GRPC_WEB_CONTENT_TYPE);
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
  encoder_callbacks_->encodingBuffer()->add(&GrpcWebFilter::GRPC_WEB_TRAILER, 1);
  trailers.iterate([](const Http::HeaderEntry& header, void* context) -> void {
    Buffer::InstancePtr& buffer =
        static_cast<GrpcWebFilter*>(context)->encoder_callbacks_->encodingBuffer();
    buffer->add(header.key().c_str(), header.key().size());
    buffer->add(":");
    buffer->add(header.value().c_str(), header.value().size());
    buffer->add("\r\n");
  }, this);
  return Http::FilterTrailersStatus::Continue;
}
} // namespace Grpc
