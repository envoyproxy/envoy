#include "test/integration/filters/tee_filter.h"

#include "envoy/registry/registry.h"

#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"

namespace Envoy {

// A test filter that essentially tees the data flow through it.
class StreamTeeFilter : public Http::PassThroughFilter, public StreamTee {
public:
  // Http::PassThroughFilter
  Http::FilterDataStatus decodeData(Buffer::Instance& buffer, bool end_stream) override {
    ENVOY_LOG_MISC(trace, "StreamTee decodeData {}", buffer.length());
    absl::MutexLock l{&mutex_};
    request_body_.add(buffer);
    decode_end_stream_ = end_stream;
    if (on_decode_data_) {
      return on_decode_data_(*this, decoder_callbacks_);
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& request_trailers) override {
    absl::MutexLock l{&mutex_};
    request_trailers_ = Http::createHeaderMap<Http::RequestTrailerMapImpl>(request_trailers);
    decode_end_stream_ = true;
    return Http::FilterTrailersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override {
    ENVOY_LOG_MISC(trace, "StreamTee encodeData {}", buffer.length());
    absl::MutexLock l{&mutex_};
    response_body_.add(buffer);
    encode_end_stream_ = end_stream;
    if (on_encode_data_) {
      return on_encode_data_(*this, encoder_callbacks_);
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& response_trailers) override {
    absl::MutexLock l{&mutex_};
    response_trailers_ = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(response_trailers);
    encode_end_stream_ = true;
    return Http::FilterTrailersStatus::Continue;
  }
};

Http::FilterFactoryCb StreamTeeFilterConfig::createFilter(const std::string&,
                                                          Server::Configuration::FactoryContext&) {
  return [this](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<StreamTeeFilter>();
    // TODO(kbaichoo): support multiple streams.
    current_tee_ = filter;
    callbacks.addStreamFilter(std::move(filter));
  };
}

bool StreamTeeFilterConfig::inspectStreamTee(int /*stream_number*/,
                                             std::function<void(const StreamTee&)> inspector) {
  if (!current_tee_) {
    ENVOY_LOG_MISC(warn, "No current stream_tee!");
    return false;
  }

  // TODO(kbaichoo): support multiple streams.
  inspector(*current_tee_);
  return true;
}

bool StreamTeeFilterConfig::setEncodeDataCallback(
    int /*stream_number*/,
    std::function<Http::FilterDataStatus(StreamTee&,
                                         Http::StreamEncoderFilterCallbacks* encoder_cbs)>
        cb) {
  if (!current_tee_) {
    ENVOY_LOG_MISC(warn, "No current stream_tee!");
    return false;
  }

  absl::MutexLock l{&current_tee_->mutex_};
  current_tee_->on_encode_data_ = cb;
  return true;
}

bool StreamTeeFilterConfig::setDecodeDataCallback(
    int /*stream_number*/,
    std::function<Http::FilterDataStatus(StreamTee&,
                                         Http::StreamDecoderFilterCallbacks* decoder_cbs)>
        cb) {
  if (!current_tee_) {
    ENVOY_LOG_MISC(warn, "No current stream_tee!");
    return false;
  }

  absl::MutexLock l{&current_tee_->mutex_};
  current_tee_->on_decode_data_ = cb;
  return true;
}

} // namespace Envoy
