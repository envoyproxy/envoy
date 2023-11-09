#pragma once
#include <memory>
#include <string>

#include "envoy/http/filter.h"
#include "envoy/server/filter_config.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/extensions/filters/http/common/empty_http_filter_config.h"

#include "absl/synchronization/mutex.h"

namespace Envoy {

// Tees stream data.
struct StreamTee {
  virtual ~StreamTee() = default;
  mutable absl::Mutex mutex_;
  Buffer::OwnedImpl request_body_ ABSL_GUARDED_BY(mutex_){};
  Buffer::OwnedImpl response_body_ ABSL_GUARDED_BY(mutex_){};
  bool decode_end_stream_ ABSL_GUARDED_BY(mutex_){false};
  bool encode_end_stream_ ABSL_GUARDED_BY(mutex_){false};
  Http::RequestHeaderMapPtr request_headers_ ABSL_GUARDED_BY(mutex_){nullptr};
  Http::RequestTrailerMapPtr request_trailers_ ABSL_GUARDED_BY(mutex_){nullptr};
  Http::ResponseHeaderMapPtr response_headers_ ABSL_GUARDED_BY(mutex_){nullptr};
  Http::ResponseTrailerMapPtr response_trailers_ ABSL_GUARDED_BY(mutex_){nullptr};

  std::function<Http::FilterDataStatus(StreamTee&, Http::StreamEncoderFilterCallbacks* encoder_cbs)>
      on_encode_data_ ABSL_GUARDED_BY(mutex_){nullptr};
  std::function<Http::FilterDataStatus(StreamTee&, Http::StreamDecoderFilterCallbacks* decoder_cbs)>
      on_decode_data_ ABSL_GUARDED_BY(mutex_){nullptr};
};

using StreamTeeSharedPtr = std::shared_ptr<StreamTee>;

// Inject a specific instance of this factory in order to leverage the same
// instance used by Envoy to inspect internally.
class StreamTeeFilterConfig : public Extensions::HttpFilters::Common::EmptyHttpFilterConfig {
public:
  StreamTeeFilterConfig() : EmptyHttpFilterConfig("stream-tee-filter") {}

  absl::StatusOr<Http::FilterFactoryCb>
  createFilter(const std::string&, Server::Configuration::FactoryContext&) override;
  bool inspectStreamTee(uint32_t stream_id, std::function<void(const StreamTee&)> inspector);
  bool setEncodeDataCallback(uint32_t stream_id,
                             std::function<Http::FilterDataStatus(
                                 StreamTee&, Http::StreamEncoderFilterCallbacks* encoder_cbs)>
                                 cb);
  bool setDecodeDataCallback(uint32_t stream_id,
                             std::function<Http::FilterDataStatus(
                                 StreamTee&, Http::StreamDecoderFilterCallbacks* decoder_cbs)>
                                 cb);
  static uint32_t computeClientStreamId(uint32_t stream_number) { return 2 * stream_number + 1; }

private:
  // TODO(kbaichoo): support multiple connections.
  absl::flat_hash_map<uint32_t, StreamTeeSharedPtr> stream_id_to_stream_tee_;
  uint32_t client_streams_created_{0};
  uint32_t consumeNextClientStreamId() { return computeClientStreamId(client_streams_created_++); }
};

} // namespace Envoy
