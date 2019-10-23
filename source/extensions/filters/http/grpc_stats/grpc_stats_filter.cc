#include "extensions/filters/http/grpc_stats/grpc_stats_filter.h"

#include "envoy/registry/registry.h"

#include "common/grpc/codec.h"
#include "common/grpc/common.h"
#include "common/grpc/context_impl.h"

#include "extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {

namespace {

class GrpcStatsFilter : public Http::PassThroughFilter {
public:
  explicit GrpcStatsFilter(Grpc::Context& context, bool emit_filter_state)
      : context_(context), emit_filter_state_(emit_filter_state) {}

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
      uint64_t delta = request_counter_.decode(data);
      if (delta > 0) {
        maybeWriteFilterState();
        if (doStatTracking()) {
          context_.chargeRequestMessageStat(*cluster_, *request_names_, delta);
        }
      }
    }
    return Http::FilterDataStatus::Continue;
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
      uint64_t delta = response_counter_.decode(data);
      if (delta > 0) {
        maybeWriteFilterState();
        if (doStatTracking()) {
          context_.chargeResponseMessageStat(*cluster_, *request_names_, delta);
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

  bool doStatTracking() const { return request_names_.has_value(); }

  void maybeWriteFilterState() {
    if (!emit_filter_state_) {
      return;
    }
    if (filter_object_ == nullptr) {
      auto state = std::make_unique<GrpcStatsObject>();
      filter_object_ = state.get();
      decoder_callbacks_->streamInfo().filterState().setData(
          HttpFilterNames::get().GrpcStats, std::move(state),
          StreamInfo::FilterState::StateType::Mutable);
    }
    filter_object_->request_message_count = request_counter_.frameCount();
    filter_object_->response_message_count = response_counter_.frameCount();
  }

private:
  Grpc::Context& context_;
  const bool emit_filter_state_;
  GrpcStatsObject* filter_object_{};
  bool grpc_request_{false};
  bool grpc_response_{false};
  Grpc::FrameInspector request_counter_;
  Grpc::FrameInspector response_counter_;
  Upstream::ClusterInfoConstSharedPtr cluster_;
  absl::optional<Grpc::Context::RequestNames> request_names_;
};

} // namespace

Http::FilterFactoryCb GrpcStatsFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::http::grpc_stats::v2alpha::FilterConfig& config,
    const std::string&, Server::Configuration::FactoryContext& factory_context) {
  return [&factory_context, emit_filter_state = config.emit_filter_state()](
             Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(
        std::make_shared<GrpcStatsFilter>(factory_context.grpcContext(), emit_filter_state));
  };
}

/**
 * Static registration for the gRPC stats filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GrpcStatsFilterConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
