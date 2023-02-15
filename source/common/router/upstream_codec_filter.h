#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.h"
#include "envoy/extensions/filters/http/upstream_codec/v3/upstream_codec.pb.validate.h"
#include "envoy/http/conn_pool.h"
#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/common/config/well_known_names.h"
#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Router {

// This is the last filter in the upstream filter chain.
// It takes request headers/body/data from the filter manager and encodes them to the upstream
// codec. It also registers the CodecBridge with the upstream stream, and takes response
// headers/body/data from the upstream stream and sends them to the filter manager.
class UpstreamCodecFilter : public Http::StreamDecoderFilter,
                            public Logger::Loggable<Logger::Id::router>,
                            public Http::DownstreamWatermarkCallbacks,
                            public Http::UpstreamCallbacks {
public:
  UpstreamCodecFilter() : bridge_(*this), calling_encode_headers_(false), deferred_reset_(false) {}

  // Http::DownstreamWatermarkCallbacks
  void onBelowWriteBufferLowWatermark() override;
  void onAboveWriteBufferHighWatermark() override;

  // UpstreamCallbacks
  void onUpstreamConnectionEstablished() override;

  // Http::StreamFilterBase
  void onDestroy() override { callbacks_->removeDownstreamWatermarkCallbacks(*this); }

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  Http::FilterMetadataStatus decodeMetadata(Http::MetadataMap& metadata_map) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // This bridge connects the upstream stream to the filter manager.
  class CodecBridge : public UpstreamToDownstream {
  public:
    CodecBridge(UpstreamCodecFilter& filter) : filter_(filter) {}
    void decode1xxHeaders(Http::ResponseHeaderMapPtr&& headers) override;
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
    void decodeMetadata(Http::MetadataMapPtr&&) override;
    void dumpState(std::ostream& os, int indent_level) const override;

    void onResetStream(Http::StreamResetReason reason,
                       absl::string_view transport_failure_reason) override {
      if (filter_.calling_encode_headers_) {
        filter_.deferred_reset_ = true;
        return;
      }
      if (reason == Http::StreamResetReason::LocalReset) {
        ASSERT(transport_failure_reason.empty());
        // Use this to communicate to the upstream request to not force-terminate.
        transport_failure_reason = "codec_error";
      }
      filter_.callbacks_->resetStream(reason, transport_failure_reason);
    }
    void onAboveWriteBufferHighWatermark() override {
      filter_.callbacks_->onDecoderFilterAboveWriteBufferHighWatermark();
    }
    void onBelowWriteBufferLowWatermark() override {
      filter_.callbacks_->onDecoderFilterBelowWriteBufferLowWatermark();
    }
    // UpstreamToDownstream
    const Route& route() const override { return *filter_.callbacks_->route(); }
    OptRef<const Network::Connection> connection() const override {
      return filter_.callbacks_->connection();
    }
    const Http::ConnectionPool::Instance::StreamOptions& upstreamStreamOptions() const override {
      return filter_.callbacks_->upstreamCallbacks()->upstreamStreamOptions();
    }

  private:
    void maybeEndDecode(bool end_stream);
    bool seen_1xx_headers_{};
    UpstreamCodecFilter& filter_;
  };
  Http::StreamDecoderFilterCallbacks* callbacks_;
  CodecBridge bridge_;
  bool calling_encode_headers_ : 1;
  bool deferred_reset_ : 1;
  OptRef<Http::RequestHeaderMap> latched_headers_;
  absl::optional<bool> latched_end_stream_;

private:
  StreamInfo::UpstreamTiming& upstreamTiming() {
    return callbacks_->upstreamCallbacks()->upstreamStreamInfo().upstreamInfo()->upstreamTiming();
  }
};

class UpstreamCodecFilterFactory
    : public Extensions::HttpFilters::Common::CommonFactoryBase<
          envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec>,
      public Server::Configuration::UpstreamHttpFilterConfigFactory {
public:
  UpstreamCodecFilterFactory() : CommonFactoryBase("envoy.filters.http.upstream_codec") {}

  std::string category() const override { return "envoy.filters.http.upstream"; }
  Http::FilterFactoryCb
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::UpstreamHttpFactoryContext&) override {
    return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
      callbacks.addStreamDecoderFilter(std::make_shared<UpstreamCodecFilter>());
    };
  }
  bool isTerminalFilterByProtoTyped(
      const envoy::extensions::filters::http::upstream_codec::v3::UpstreamCodec&,
      Server::Configuration::ServerFactoryContext&) override {
    return true;
  }
};

DECLARE_FACTORY(UpstreamCodecFilterFactory);

} // namespace Router
} // namespace Envoy
