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
    absl::MutexLock l{mutex_};
    request_body_.add(buffer);
    decode_end_stream_ = end_stream;
    if (on_decode_data_) {
      return on_decode_data_(*this, decoder_callbacks_);
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& request_trailers) override {
    absl::MutexLock l{mutex_};
    request_trailers_ = Http::createHeaderMap<Http::RequestTrailerMapImpl>(request_trailers);
    decode_end_stream_ = true;
    return Http::FilterTrailersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& buffer, bool end_stream) override {
    ENVOY_LOG_MISC(trace, "StreamTee encodeData {}", buffer.length());
    absl::MutexLock l{mutex_};
    response_body_.add(buffer);
    encode_end_stream_ = end_stream;
    if (on_encode_data_) {
      return on_encode_data_(*this, encoder_callbacks_);
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& response_trailers) override {
    absl::MutexLock l{mutex_};
    response_trailers_ = Http::createHeaderMap<Http::ResponseTrailerMapImpl>(response_trailers);
    encode_end_stream_ = true;
    return Http::FilterTrailersStatus::Continue;
  }
};

absl::StatusOr<Http::FilterFactoryCb>
StreamTeeFilterConfig::createFilter(const std::string&, Server::Configuration::FactoryContext&) {
  return [this](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto filter = std::make_shared<StreamTeeFilter>();
    // TODO(kbaichoo): support multiple connections.
    uint32_t next_stream_id = consumeNextClientStreamId();
    ASSERT(!stream_id_to_stream_tee_.contains(next_stream_id), "Client Stream ID already exists.");
    stream_id_to_stream_tee_.insert({next_stream_id, filter});
    callbacks.addStreamFilter(std::move(filter));
  };
}

bool StreamTeeFilterConfig::inspectStreamTee(uint32_t stream_id,
                                             std::function<void(const StreamTee&)> inspector) {
  if (!stream_id_to_stream_tee_.contains(stream_id)) {
    ENVOY_LOG_MISC(warn, "No stream with the given ID.");
    return false;
  }

  const StreamTeeSharedPtr& stream_tee = stream_id_to_stream_tee_.find(stream_id)->second;
  inspector(*stream_tee);
  return true;
}

bool StreamTeeFilterConfig::setEncodeDataCallback(
    uint32_t stream_id,
    std::function<Http::FilterDataStatus(StreamTee&,
                                         Http::StreamEncoderFilterCallbacks* encoder_cbs)>
        cb) {
  if (!stream_id_to_stream_tee_.contains(stream_id)) {
    ENVOY_LOG_MISC(warn, "No stream with the given ID.");
    return false;
  }

  StreamTeeSharedPtr& stream_tee = stream_id_to_stream_tee_.find(stream_id)->second;
  absl::MutexLock l{stream_tee->mutex_};
  stream_tee->on_encode_data_ = cb;

  return true;
}

bool StreamTeeFilterConfig::setDecodeDataCallback(
    uint32_t stream_id,
    std::function<Http::FilterDataStatus(StreamTee&,
                                         Http::StreamDecoderFilterCallbacks* decoder_cbs)>
        cb) {
  if (!stream_id_to_stream_tee_.contains(stream_id)) {
    ENVOY_LOG_MISC(warn, "No stream with the given ID.");
    return false;
  }

  StreamTeeSharedPtr& stream_tee = stream_id_to_stream_tee_.find(stream_id)->second;
  absl::MutexLock l{stream_tee->mutex_};
  stream_tee->on_decode_data_ = cb;
  return true;
}

} // namespace Envoy
