#include "extensions/filters/http/grpc_stats/config.h"

#include "envoy/http/filter.h"
#include "envoy/registry/registry.h"

#include "common/grpc/common.h"
#include "common/grpc/context_impl.h"

#include "extensions/filters/http/grpc_stats/message_counter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {

namespace {

class GrpcStatsFilter : public Http::StreamFilter {
public:
  explicit GrpcStatsFilter(Grpc::Context& context) : context_(context) {}

  // Http::StreamFilterBase
  void onDestroy() override {}

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool) override {
    grpc_request_ = Grpc::Common::hasGrpcContentType(headers);
    if (grpc_request_) {
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
        maybeWriteState();
        if (doStatTracking()) {
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
    if (doStatTracking()) {
      context_.chargeStat(*cluster_, Grpc::Context::Protocol::Grpc, *request_names_,
                          headers.GrpcStatus());
    }
    return Http::FilterHeadersStatus::Continue;
  }
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool) override {
    if (grpc_response_) {
      uint64_t delta = IncrementMessageCounter(data, &response_counter_);
      if (delta > 0) {
        maybeWriteState();
        if (cluster_ && request_names_) {
          context_.chargeResponseStat(*cluster_, *request_names_, delta);
        }
      }
    }
    return Http::FilterDataStatus::Continue;
  }
  Http::FilterTrailersStatus encodeTrailers(Http::HeaderMap& trailers) override {
    if (doStatTracking()) {
      context_.chargeStat(*cluster_, Grpc::Context::Protocol::Grpc, *request_names_,
                          trailers.GrpcStatus());
    }
    return Http::FilterTrailersStatus::Continue;
  }
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap&) override {
    return Http::FilterMetadataStatus::Continue;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks&) override {}

  void maybeWriteState() {
    if (filter_object_ == nullptr) {
      auto state = std::make_unique<GrpcMessageCounterObject>();
      filter_object_ = state.get();
      decoder_callbacks_->streamInfo().filterState().setData(
          HttpFilterNames::get().GrpcStats, std::move(state),
          StreamInfo::FilterState::StateType::Mutable);
    }
    filter_object_->request_message_count = request_counter_.count;
    filter_object_->response_message_count = response_counter_.count;
  }

  bool doStatTracking() const { return request_names_.has_value(); }

private:
  Grpc::Context& context_;
  bool grpc_request_{false};
  bool grpc_response_{false};
  GrpcMessageCounter request_counter_;
  GrpcMessageCounter response_counter_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  absl::optional<Grpc::Context::RequestNames> request_names_;
  GrpcMessageCounterObject* filter_object_{};
};

} // namespace

Http::FilterFactoryCb
GrpcStatsFilterConfig::createFilter(const std::string&,
                                    Server::Configuration::FactoryContext& factory_context) {
  return [&factory_context](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<GrpcStatsFilter>(factory_context.grpcContext()));
  };
}

/**
 * Static registration for the gRPC-Web filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GrpcStatsFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
