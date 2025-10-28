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
#include "source/common/router/upstream_to_downstream_impl_base.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/http/common/factory_base.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Router {

// This is the last filter in the upstream HTTP filter chain.
// It takes request headers/body/data from the filter manager and encodes them to the upstream
// codec. It also registers the CodecBridge with the upstream stream, and takes response
// headers/body/data from the upstream stream and sends them to the filter manager.
class UpstreamCodecFilter : public Http::StreamDecoderFilter,
                            public Logger::Loggable<Logger::Id::router>,
                            public Http::DownstreamWatermarkCallbacks,
                            public Http::UpstreamCallbacks {
public:
  UpstreamCodecFilter() : bridge_(*this), deferred_reset_status_(absl::OkStatus()) {}

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
  class CodecBridge : public UpstreamToDownstreamImplBase {
  public:
    CodecBridge(UpstreamCodecFilter& filter) : filter_(filter) {}
    void decode1xxHeaders(Http::ResponseHeaderMapPtr&& headers) override;
    void decodeHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) override;
    void decodeData(Buffer::Instance& data, bool end_stream) override;
    void decodeTrailers(Http::ResponseTrailerMapPtr&& trailers) override;
    void decodeMetadata(Http::MetadataMapPtr&&) override;
    void dumpState(std::ostream& os, int indent_level) const override;
    void onResetStream(Http::StreamResetReason reason,
                       absl::string_view transport_failure_reason) override;

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
    bool first_body_rx_recorded_{};
    UpstreamCodecFilter& filter_;
  };
  Http::StreamDecoderFilterCallbacks* callbacks_;
  CodecBridge bridge_;
  OptRef<Http::RequestHeaderMap> latched_headers_;
  absl::Status deferred_reset_status_;
  absl::optional<bool> latched_end_stream_;
  // Keep small members (bools and enums) at the end of class, to reduce alignment overhead.
  bool calling_encode_headers_ = false;

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
  absl::StatusOr<Http::FilterFactoryCb>
  createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                               Server::Configuration::UpstreamFactoryContext&) override {
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

const Http::FilterChainFactory& defaultUpstreamHttpFilterChainFactory();

} // namespace Router
} // namespace Envoy
