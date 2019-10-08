#include "extensions/filters/http/grpc_streaming/config.h"

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "common/grpc/common.h"

#include "extensions/filters/http/grpc_streaming/message_counter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStreaming {

namespace {

class GrpcStreamingFilter : public Http::StreamFilter {
public:
  GrpcStreamingFilter() { maybeWriteState(true, true); }

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool) override {
    if (Grpc::Common::hasGrpcContentType(headers)) {
      grpc_request_ = true;
    }
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    if (grpc_request_) {
      if (IncrementMessageCounter(data, &request_counter_)) {
        maybeWriteState(true, false);
      }
    }
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& decoder_callbacks) override {
    filter_state_ = &decoder_callbacks.streamInfo().filterState();
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override {
    if (Grpc::Common::isGrpcResponseHeader(headers, end_stream)) {
      grpc_response_ = true;
    }
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool) override {
    if (grpc_response_) {
      if (IncrementMessageCounter(data, &response_counter_)) {
        maybeWriteState(false, true);
      }
    }
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) override {}

  void maybeWriteState(bool update_request, bool update_response) {
    if (filter_state_ == nullptr) {
      return;
    }
    if (!filter_state_->hasDataWithName(HttpFilterNames::get().GrpcStreaming)) {
      filter_state_->setData(HttpFilterNames::get().GrpcStreaming,
                             std::make_unique<GrpcMessageCounterObject>(),
                             StreamInfo::FilterState::StateType::Mutable);
    }

    auto& data = filter_state_->getDataMutable<GrpcMessageCounterObject>(
        HttpFilterNames::get().GrpcStreaming);
    if (update_request) {
      data.request_message_count = request_counter_.count;
    }
    if (update_response) {
      data.response_message_count = response_counter_.count;
    }
  }

private:
  StreamInfo::FilterState* filter_state_{nullptr};
  bool grpc_request_{false};
  bool grpc_response_{false};
  GrpcMessageCounter request_counter_;
  GrpcMessageCounter response_counter_;
};

} // namespace

Http::FilterFactoryCb
GrpcStreamingFilterConfig::createFilter(const std::string&,
                                        Server::Configuration::FactoryContext&) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<GrpcStreamingFilter>());
  };
}

/**
 * Static registration for the gRPC-Web filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GrpcStreamingFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcStreaming
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
