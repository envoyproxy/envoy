#include "extensions/filters/http/grpc_streaming/config.h"

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "common/grpc/common.h"
#include "common/grpc/context_impl.h"

#include "extensions/filters/http/grpc_streaming/message_counter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStreaming {

namespace {

class GrpcStreamingFilter : public Http::StreamFilter {
public:
  explicit GrpcStreamingFilter(Grpc::Context& context) : context_(context) {}

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool) override {
    grpc_request_ = Grpc::Common::hasGrpcContentType(headers);
    if (grpc_request_ && decoder_callbacks_) {
      cluster_ = decoder_callbacks_->clusterInfo();
      if (cluster_) {
        request_names_ = context_.resolveServiceAndMethod(headers.Path());
      }
    }
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool) override {
    if (grpc_request_) {
      uint64_t delta = IncrementMessageCounter(data, &request_counter_);
      if (delta > 0) {
        maybeWriteState(true, false);
        if (cluster_ && request_names_) {
          context_.chargeRequestStat(*cluster_, *request_names_, delta);
        }
      }
    }
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap&) override {
    return Http::FilterTrailersStatus::Continue;
  }
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Http::StreamEncoderFilter
  Http::FilterHeadersStatus encode100ContinueHeaders(Http::HeaderMap&) override {
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterHeadersStatus encodeHeaders(Http::HeaderMap& headers, bool end_stream) override {
    grpc_response_ = Grpc::Common::isGrpcResponseHeader(headers, end_stream);
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool) override {
    if (grpc_response_) {
      uint64_t delta = IncrementMessageCounter(data, &response_counter_);
      if (delta > 0) {
        maybeWriteState(false, true);
        if (cluster_ && request_names_) {
          context_.chargeResponseStat(*cluster_, *request_names_, delta);
        }
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
    if (decoder_callbacks_ == nullptr) {
      return;
    }
    auto& filter_state = decoder_callbacks_->streamInfo().filterState();
    if (!filter_state.hasDataWithName(HttpFilterNames::get().GrpcStreaming)) {
      filter_state.setData(HttpFilterNames::get().GrpcStreaming,
                           std::make_unique<GrpcMessageCounterObject>(),
                           StreamInfo::FilterState::StateType::Mutable);
    }

    auto& data =
        filter_state.getDataMutable<GrpcMessageCounterObject>(HttpFilterNames::get().GrpcStreaming);
    if (update_request) {
      data.request_message_count = request_counter_.count;
    }
    if (update_response) {
      data.response_message_count = response_counter_.count;
    }
  }

private:
  Grpc::Context& context_;
  bool grpc_request_{false};
  bool grpc_response_{false};
  GrpcMessageCounter request_counter_;
  GrpcMessageCounter response_counter_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  absl::optional<Grpc::Context::RequestNames> request_names_;
};

} // namespace

Http::FilterFactoryCb
GrpcStreamingFilterConfig::createFilter(const std::string&,
                                        Server::Configuration::FactoryContext& factory_context) {
  return [&factory_context](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<GrpcStreamingFilter>(factory_context.grpcContext()));
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
