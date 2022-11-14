#include "source/extensions/filters/http/connect_stats/connect_stats_filter.h"

#include <memory>

#include "envoy/extensions/filters/http/connect_stats/v3/config.pb.h"
#include "envoy/extensions/filters/http/connect_stats/v3/config.pb.validate.h"
#include "envoy/grpc/context.h"
#include "envoy/registry/registry.h"

#include "source/common/grpc/common.h"
#include "source/common/grpc/context_impl.h"
#include "source/common/runtime/runtime_impl.h"
#include "source/common/stats/symbol_table.h"
#include "source/common/stream_info/utility.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/connect_stats/response_frame_counter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectStats {

namespace {

// TODO(jchadwick-buf): This is a copy of GrpcServiceMethodToRequestNamesMap from
// grpc_stats_filter.cc. Either this extension should be somehow merged with gRPC Stats, or at
// least some code should probably be shared.
class ConnectServiceMethodToRequestNamesMap {
public:
  // Construct a map populated with the services/methods in method_list.
  ConnectServiceMethodToRequestNamesMap(Stats::SymbolTable& symbol_table,
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
  Config(const envoy::extensions::filters::http::connect_stats::v3::FilterConfig& proto_config,
         Server::Configuration::FactoryContext& context)
      : context_(context.grpcContext()), emit_filter_state_(proto_config.emit_filter_state()),
        enable_upstream_stats_(proto_config.enable_upstream_stats()),
        replace_dots_in_connect_service_name_(proto_config.replace_dots_in_connect_service_name()) {

    switch (proto_config.per_method_stat_specifier_case()) {
    case envoy::extensions::filters::http::connect_stats::v3::FilterConfig::
        PER_METHOD_STAT_SPECIFIER_NOT_SET:
    case envoy::extensions::filters::http::connect_stats::v3::FilterConfig::kStatsForAllMethods:
      if (proto_config.has_stats_for_all_methods()) {
        stats_for_all_methods_ = proto_config.stats_for_all_methods().value();
      }
      break;

    case envoy::extensions::filters::http::connect_stats::v3::FilterConfig::
        kIndividualMethodStatsAllowlist:
      allowlist_.emplace(context.scope().symbolTable(),
                         proto_config.individual_method_stats_allowlist());
      break;
    }
  }
  Grpc::Context& context_;
  const bool emit_filter_state_;
  const bool enable_upstream_stats_;
  const bool replace_dots_in_connect_service_name_;
  bool stats_for_all_methods_{false};
  absl::optional<ConnectServiceMethodToRequestNamesMap> allowlist_;
};
using ConfigConstSharedPtr = std::shared_ptr<const Config>;

class ConnectStatsFilter : public Http::PassThroughFilter {
public:
  ConnectStatsFilter(ConfigConstSharedPtr config) : config_(config) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override {
    cluster_ = decoder_callbacks_->clusterInfo();
    if (cluster_) {
      connect_streaming_request_ = Grpc::Common::isConnectStreamingRequestHeaders(headers);
      connect_request_ =
          connect_streaming_request_ || Grpc::Common::isConnectRequestHeaders(headers);

      if (connect_request_) {
        if (config_->stats_for_all_methods_) {
          if (config_->replace_dots_in_connect_service_name_) {
            request_names_ =
                config_->context_.resolveDynamicServiceAndMethodWithDotReplaced(headers.Path());
          } else {
            request_names_ = config_->context_.resolveDynamicServiceAndMethod(headers.Path());
          }
        } else {
          auto request_names = Grpc::Common::resolveServiceAndMethod(headers.Path());
          if (request_names && config_->allowlist_) {
            request_names_ = config_->allowlist_->lookup(*request_names);
          }
        }

        if (!connect_streaming_request_ && end_stream) {
          config_->context_.chargeRequestMessageStat(*cluster_, request_names_, 1);
        }
      }
    }

    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override {
    if (connect_streaming_request_) {
      uint64_t delta = request_counter_.inspect(data);
      if (delta > 0) {
        maybeWriteFilterState();
        config_->context_.chargeRequestMessageStat(*cluster_, request_names_, delta);
      }
    } else if (connect_request_ && end_stream) {
      config_->context_.chargeRequestMessageStat(*cluster_, request_names_, 1);
    }
    return Http::FilterDataStatus::Continue;
  }

  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override {
    if (connect_request_) {
      if (!connect_streaming_request_) {
        config_->context_.chargeResponseMessageStat(*cluster_, request_names_, 1);
        config_->context_.chargeStat(*cluster_, Grpc::Context::Protocol::Grpc, request_names_,
                                     headers.getStatusValue() == "200");
      }
      if (end_stream) {
        maybeChargeUpstreamStat();
      }
    }
    return Http::FilterHeadersStatus::Continue;
  }

  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override {
    if (connect_streaming_request_) {
      uint64_t delta = response_counter_.inspect(data);
      if (delta > 0) {
        maybeWriteFilterState();
        config_->context_.chargeResponseMessageStat(*cluster_, request_names_, delta);
      }
      if (response_counter_.endStream()) {
        config_->context_.chargeStat(*cluster_, Grpc::Context::Protocol::Grpc, request_names_,
                                     response_counter_.statusCode().empty());
        maybeChargeUpstreamStat();
      }
    } else if (connect_request_ && end_stream) {
      config_->context_.chargeResponseMessageStat(*cluster_, request_names_, 1);
      maybeChargeUpstreamStat();
    }

    return Http::FilterDataStatus::Continue;
  }

  void maybeWriteFilterState() {
    if (!config_->emit_filter_state_) {
      return;
    }
    if (filter_object_ == nullptr) {
      auto state = std::make_unique<ConnectStatsObject>();
      filter_object_ = state.get();
      decoder_callbacks_->streamInfo().filterState()->setData(
          "envoy.filters.http.connect_stats", std::move(state),
          StreamInfo::FilterState::StateType::Mutable,
          StreamInfo::FilterState::LifeSpan::FilterChain);
    }
    if (connect_streaming_request_) {
      filter_object_->request_message_count = request_counter_.frameCount();
      filter_object_->response_message_count = response_counter_.frameCount();
    } else if (connect_request_) {
      filter_object_->request_message_count = 1;
      filter_object_->response_message_count = 1;
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
  ConnectStatsObject* filter_object_{};
  bool connect_request_{false};
  bool connect_streaming_request_{false};
  Grpc::FrameInspector request_counter_;
  ConnectResponseFrameCounter response_counter_;
  Upstream::ClusterInfoConstSharedPtr cluster_;
  absl::optional<Grpc::Context::RequestStatNames> request_names_;
};

} // namespace

Http::FilterFactoryCb ConnectStatsFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::connect_stats::v3::FilterConfig& proto_config,
    const std::string&, Server::Configuration::FactoryContext& factory_context) {

  ConfigConstSharedPtr config = std::make_shared<const Config>(proto_config, factory_context);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<ConnectStatsFilter>(config));
  };
}

/**
 * Static registration for the Connect stats filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ConnectStatsFilterConfigFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ConnectStats
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
