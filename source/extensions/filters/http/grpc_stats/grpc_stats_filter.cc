#include "source/extensions/filters/http/grpc_stats/grpc_stats_filter.h"

#include "envoy/extensions/filters/http/grpc_stats/v3/config.pb.h"
#include "envoy/extensions/filters/http/grpc_stats/v3/config.pb.validate.h"
#include "envoy/grpc/context.h"
#include "envoy/registry/registry.h"

#include "source/common/grpc/codec.h"
#include "source/common/grpc/common.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stream_info/utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/grpc_stats/response_frame_counter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcStats {

namespace {

// A map from gRPC service/method name to symbolized stat names for the service/method.
//
// The expected usage pattern is that the map is populated once, and can then be queried lock-free
// as long as it isn't being modified.
class GrpcServiceMethodToRequestNamesMap {
public:
  // Construct a map populated with the services/methods in method_list.
  GrpcServiceMethodToRequestNamesMap(Stats::SymbolTable& symbol_table,
                                     const envoy::config::core::v3::GrpcMethodList& method_list)
      : stat_name_pool_(symbol_table), map_(populate(method_list)) {}

  absl::optional<Grpc::Context::RequestStatNames>
  lookup(const Grpc::Common::RequestNames& request_names) const {
    auto it = map_.find(request_names);
    if (it != map_.end()) {
      return it->second;
    }

    return {};
  }

private:
  using OwningKey = std::tuple<std::string, std::string>;
  using ViewKey = Grpc::Common::RequestNames;

  class MapHash {
  private:
    // Use the same type for hashing all variations to ensure the same hash value from all source
    // types.
    using ViewTuple = std::tuple<absl::string_view, absl::string_view>;
    static uint64_t hash(const ViewTuple& key) { return absl::Hash<ViewTuple>()(key); }

  public:
    using is_transparent = void; // NOLINT(readability-identifier-naming)

    uint64_t operator()(const OwningKey& key) const { return hash(key); }
    uint64_t operator()(const ViewKey& key) const {
      return hash(ViewTuple(key.service_, key.method_));
    }
  };

  struct MapEq {
    using is_transparent = void; // NOLINT(readability-identifier-naming)
    bool operator()(const OwningKey& left, const OwningKey& right) const { return left == right; }
    bool operator()(const OwningKey& left, const ViewKey& right) const {
      return left == std::make_tuple(right.service_, right.method_);
    }
  };
  using MapType = absl::flat_hash_map<OwningKey, Grpc::Context::RequestStatNames, MapHash, MapEq>;

  // Helper for generating a populated MapType so that `map_` can be const.
  MapType populate(const envoy::config::core::v3::GrpcMethodList& method_list) {
    MapType map;
    for (const auto& service : method_list.services()) {
      Stats::StatName stat_name_service = stat_name_pool_.add(service.name());

      for (const auto& method_name : service.method_names()) {
        Stats::StatName stat_name_method = stat_name_pool_.add(method_name);
        map[OwningKey(service.name(), method_name)] =
            Grpc::Context::RequestStatNames{stat_name_service, stat_name_method};
      }
    }
    return map;
  }

  Stats::StatNamePool stat_name_pool_;
  const MapType map_;
};

struct Config {
  Config(const envoy::extensions::filters::http::grpc_stats::v3::FilterConfig& proto_config,
         Server::Configuration::FactoryContext& context)
      : context_(context.serverFactoryContext().grpcContext()),
        emit_filter_state_(proto_config.emit_filter_state()),
        enable_upstream_stats_(proto_config.enable_upstream_stats()),
        replace_dots_in_grpc_service_name_(proto_config.replace_dots_in_grpc_service_name()),
        stats_for_all_methods_(
            PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, stats_for_all_methods, false)) {

    if (proto_config.per_method_stat_specifier_case() ==
        envoy::extensions::filters::http::grpc_stats::v3::FilterConfig::
            kIndividualMethodStatsAllowlist) {
      allowlist_.emplace(context.scope().symbolTable(),
                         proto_config.individual_method_stats_allowlist());
    }
  }
  Grpc::Context& context_;
  const bool emit_filter_state_;
  const bool enable_upstream_stats_;
  const bool replace_dots_in_grpc_service_name_;
  const bool stats_for_all_methods_;
  absl::optional<GrpcServiceMethodToRequestNamesMap> allowlist_;
};
using ConfigConstSharedPtr = std::shared_ptr<const Config>;

class GrpcStatsFilter : public Http::PassThroughFilter {
public:
  GrpcStatsFilter(ConfigConstSharedPtr config) : config_(config) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override {
    grpc_request_ = Grpc::Common::isGrpcRequestHeaders(headers);
    connect_unary_ = Grpc::Common::isConnectRequestHeaders(headers);
    connect_streaming_request_ = Grpc::Common::isConnectStreamingRequestHeaders(headers);
    if (grpc_request_ || connect_streaming_request_ || connect_unary_) {
      cluster_ = decoder_callbacks_->clusterInfo();
      if (cluster_) {
        if (config_->stats_for_all_methods_) {
          // Get dynamically-allocated Context::RequestStatNames from the context.
          if (config_->replace_dots_in_grpc_service_name_) {
            request_names_ =
                config_->context_.resolveDynamicServiceAndMethodWithDotReplaced(headers.Path());
          } else {
            request_names_ = config_->context_.resolveDynamicServiceAndMethod(headers.Path());
          }
          do_stat_tracking_ = request_names_.has_value();
        } else {
          // This case handles both proto_config.stats_for_all_methods() == false,
          // and proto_config.has_individual_method_stats_allowlist(). This works
          // because proto_config.stats_for_all_methods() == false results in
          // an empty allowlist, which exactly matches the behavior specified for
          // this configuration.
          //
          // Resolve the service and method to a string_view, then get
          // the Context::RequestStatNames out of the pre-allocated list that
          // can be produced with the allowlist being present.
          absl::optional<Grpc::Common::RequestNames> request_names =
              Grpc::Common::resolveServiceAndMethod(headers.Path());

          if (request_names) {
            // Do stat tracking as long as this looks like a grpc service/method,
            // even if it isn't in the allowlist. Things not in the allowlist
            // are counted with a stat with no service/method in the name.
            do_stat_tracking_ = true;

            // If the entry is not found in the allowlist, this will return
            // an empty optional; each of the `charge` functions on the context
            // will interpret an empty optional for this value to mean that the
            // service.method prefix on the stat should be omitted.
            if (config_->allowlist_) {
              request_names_ = config_->allowlist_->lookup(*request_names);
            }
          }
        }
      }
    }

    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    if (grpc_request_) {
      uint64_t delta = request_counter_.inspect(data);
      if (delta > 0) {
        maybeWriteFilterState();
        if (doStatTracking()) {
          config_->context_.chargeRequestMessageStat(*cluster_, request_names_, delta);
        }
      }
    } else if (connect_streaming_request_) {
      uint64_t delta = request_counter_.inspect(data);
      if (delta > 0) {
        maybeWriteFilterState();
        config_->context_.chargeRequestMessageStat(*cluster_, request_names_, delta);
      }
    } else if (connect_unary_ && end_stream) {
      connect_unary_request_body_ = true;
      maybeWriteFilterState();
      config_->context_.chargeRequestMessageStat(*cluster_, request_names_, 1);
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override {
    grpc_response_ = Grpc::Common::isGrpcResponseHeaders(headers, end_stream);
    connect_streaming_response_ = Grpc::Common::isConnectStreamingResponseHeaders(headers);
    if (doStatTracking()) {
      if (connect_unary_) {
        config_->context_.chargeStat(*cluster_, Grpc::Context::Protocol::Grpc, request_names_,
                                     headers.getStatusValue() == "200");
      } else if (!connect_streaming_response_) {
        config_->context_.chargeStat(*cluster_, Grpc::Context::Protocol::Grpc, request_names_,
                                     headers.GrpcStatus());
      }
      if (end_stream) {
        maybeChargeUpstreamStat();
      }
    }
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    if (grpc_response_) {
      uint64_t delta = response_counter_.inspect(data);
      if (delta > 0) {
        maybeWriteFilterState();
        if (doStatTracking()) {
          config_->context_.chargeResponseMessageStat(*cluster_, request_names_, delta);
        }
      }
    } else if (connect_streaming_request_) {
      uint64_t delta = response_counter_.inspect(data);
      if (delta > 0) {
        maybeWriteFilterState();
        config_->context_.chargeResponseMessageStat(*cluster_, request_names_, delta);
      }
      if (end_stream) {
        config_->context_.chargeStat(*cluster_, Grpc::Context::Protocol::Grpc, request_names_,
                                     response_counter_.connectSuccess());
        maybeChargeUpstreamStat();
      }
    } else if (connect_unary_ && end_stream) {
      connect_unary_response_body_ = true;
      maybeWriteFilterState();
      config_->context_.chargeResponseMessageStat(*cluster_, request_names_, 1);
      maybeChargeUpstreamStat();
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override {
    if (grpc_request_ && doStatTracking()) {
      config_->context_.chargeStat(*cluster_, Grpc::Context::Protocol::Grpc, request_names_,
                                   trailers.GrpcStatus());
      maybeChargeUpstreamStat();
    }
    return Http::FilterTrailersStatus::Continue;
  }

  bool doStatTracking() const { return do_stat_tracking_; }

  void maybeWriteFilterState() {
    if (!config_->emit_filter_state_) {
      return;
    }
    if (filter_object_ == nullptr) {
      auto state = std::make_unique<GrpcStatsObject>();
      filter_object_ = state.get();
      decoder_callbacks_->streamInfo().filterState()->setData(
          "envoy.filters.http.grpc_stats", std::move(state),
          StreamInfo::FilterState::StateType::Mutable,
          StreamInfo::FilterState::LifeSpan::FilterChain);
    }
    if (connect_unary_) {
      filter_object_->request_message_count = connect_unary_request_body_;
      filter_object_->response_message_count = connect_unary_response_body_;
    } else {
      filter_object_->request_message_count = request_counter_.frameCount();
      filter_object_->response_message_count = response_counter_.frameCount();
    }
  }

  void maybeChargeUpstreamStat() {
    if (!config_->enable_upstream_stats_) {
      return;
    }
    StreamInfo::TimingUtility timing(decoder_callbacks_->streamInfo());
    if (config_->enable_upstream_stats_ && timing.lastUpstreamTxByteSent().has_value() &&
        timing.lastUpstreamRxByteReceived().has_value()) {
      std::chrono::milliseconds chrono_duration =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              timing.lastUpstreamRxByteReceived().value() -
              timing.lastUpstreamTxByteSent().value());
      config_->context_.chargeUpstreamStat(*cluster_, request_names_, chrono_duration);
    }
  }

private:
  ConfigConstSharedPtr config_;
  GrpcStatsObject* filter_object_{};
  bool do_stat_tracking_{false};
  bool grpc_request_{false};
  bool grpc_response_{false};
  bool connect_unary_{false};
  bool connect_streaming_request_{false};
  bool connect_streaming_response_{false};
  bool connect_unary_request_body_{false};
  bool connect_unary_response_body_{false};
  Grpc::FrameInspector request_counter_;
  ResponseFrameCounter response_counter_;
  Upstream::ClusterInfoConstSharedPtr cluster_;
  absl::optional<Grpc::Context::RequestStatNames> request_names_;
};

} // namespace

Http::FilterFactoryCb GrpcStatsFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_stats::v3::FilterConfig& proto_config,
    const std::string&, Server::Configuration::FactoryContext& factory_context) {

  ConfigConstSharedPtr config = std::make_shared<const Config>(proto_config, factory_context);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<GrpcStatsFilter>(config));
  };
}

/**
 * Static registration for the gRPC stats filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GrpcStatsFilterConfigFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
