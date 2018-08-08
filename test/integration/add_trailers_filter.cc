#include "test/integration/add_trailers_filter.h"

#include <string>

namespace Envoy {
Http::FilterDataStatus AddTrailersStreamFilter::decodeData(Buffer::Instance&, bool end_stream) {
  if (end_stream) {
    decoder_callbacks_->addDecodedTrailers().insertGrpcMessage().value(std::string("decode"));
  }

  return Http::FilterDataStatus::Continue;
}

Http::FilterDataStatus AddTrailersStreamFilter::encodeData(Buffer::Instance&, bool end_stream) {
  if (end_stream) {
    encoder_callbacks_->addEncodedTrailers().insertGrpcMessage().value(std::string("encode"));
  }

  return Http::FilterDataStatus::Continue;
}
} // namespace Envoy
