#pragma once

#include <chrono>
#include <list>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "envoy/admin/v2alpha/clusters.pb.h"
#include "envoy/admin/v2alpha/listeners.pb.h"
#include "envoy/http/filter.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/runtime/runtime.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"
#include "envoy/server/listener_manager.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/resource_manager.h"

#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/common/macros.h"
#include "common/http/conn_manager_impl.h"
#include "common/http/date_provider_impl.h"
#include "common/http/default_server_string.h"
#include "common/http/utility.h"
#include "common/network/connection_balancer_impl.h"
#include "common/network/raw_buffer_socket.h"
#include "common/router/scoped_config_impl.h"
#include "common/stats/isolated_store_impl.h"

#include "server/http/config_tracker_impl.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

namespace Utility {
envoy::admin::v2alpha::ServerInfo::State serverState(Init::Manager::State state,
                                                     bool health_check_failed);
} // namespace Utility

class AdminInternalAddressConfig : public Http::InternalAddressConfig {
  bool isInternalAddress(const Network::Address::Instance&) const override { return false; }
};

/**
 * Implementation of Server::Admin.
 */
class AdminImpl : public Admin,
                  public Network::FilterChainManager,
                  public Network::FilterChainFactory,
                  public Http::FilterChainFactory,
                  public Http::ConnectionManagerConfig,
                  Logger::Loggable<Logger::Id::admin> {
public:
  AdminImpl(const std::string& profile_path, Server::Instance& server);

  Http::Code runCallback(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                         Buffer::Instance& response, AdminStream& admin_stream);
  const Network::Socket& socket() override { return *socket_; }
  Network::Socket& mutable_socket() { return *socket_; }

  // Server::Admin
  // TODO(jsedgwick) These can be managed with a generic version of ConfigTracker.
  // Wins would be no manual removeHandler() and code reuse.
  //
  // The prefix must start with "/" and contain at least one additional character.
  bool addHandler(const std::string& prefix, const std::string& help_text, HandlerCb callback,
                  bool removable, bool mutates_server_state) override;
  bool removeHandler(const std::string& prefix) override;
  ConfigTracker& getConfigTracker() override;

  void startHttpListener(const std::string& access_log_path, const std::string& address_out_path,
                         Network::Address::InstanceConstSharedPtr address,
                         const Network::Socket::OptionsSharedPtr& socket_options,
                         Stats::ScopePtr&& listener_scope) override;

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&) const override {
    return admin_filter_chain_.get();
  }

  // Network::FilterChainFactory
  bool
  createNetworkFilterChain(Network::Connection& connection,
                           const std::vector<Network::FilterFactoryCb>& filter_factories) override;
  bool createListenerFilterChain(Network::ListenerFilterManager&) override { return true; }
  bool createUdpListenerFilterChain(Network::UdpListenerFilterManager&,
                                    Network::UdpReadFilterCallbacks&) override {
    return true;
  }

  // Http::FilterChainFactory
  void createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) override;
  bool createUpgradeFilterChain(absl::string_view, const Http::FilterChainFactory::UpgradeMap*,
                                Http::FilterChainFactoryCallbacks&) override {
    return false;
  }

  // Http::ConnectionManagerConfig
  const std::list<AccessLog::InstanceSharedPtr>& accessLogs() override { return access_logs_; }
  Http::ServerConnectionPtr createCodec(Network::Connection& connection,
                                        const Buffer::Instance& data,
                                        Http::ServerConnectionCallbacks& callbacks) override;
  Http::DateProvider& dateProvider() override { return date_provider_; }
  std::chrono::milliseconds drainTimeout() override { return std::chrono::milliseconds(100); }
  Http::FilterChainFactory& filterFactory() override { return *this; }
  bool generateRequestId() override { return false; }
  bool preserveExternalRequestId() const override { return false; }
  absl::optional<std::chrono::milliseconds> idleTimeout() const override { return idle_timeout_; }
  bool isRoutable() const override { return false; }
  absl::optional<std::chrono::milliseconds> maxConnectionDuration() const override {
    return max_connection_duration_;
  }
  uint32_t maxRequestHeadersKb() const override { return max_request_headers_kb_; }
  uint32_t maxRequestHeadersCount() const override { return max_request_headers_count_; }
  std::chrono::milliseconds streamIdleTimeout() const override { return {}; }
  std::chrono::milliseconds requestTimeout() const override { return {}; }
  std::chrono::milliseconds delayedCloseTimeout() const override { return {}; }
  Router::RouteConfigProvider* routeConfigProvider() override { return &route_config_provider_; }
  Config::ConfigProvider* scopedRouteConfigProvider() override {
    return &scoped_route_config_provider_;
  }
  const std::string& serverName() override { return Http::DefaultServerString::get(); }
  HttpConnectionManagerProto::ServerHeaderTransformation serverHeaderTransformation() override {
    return HttpConnectionManagerProto::OVERWRITE;
  }
  Http::ConnectionManagerStats& stats() override { return stats_; }
  Http::ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
  bool useRemoteAddress() override { return true; }
  const Http::InternalAddressConfig& internalAddressConfig() const override {
    return internal_address_config_;
  }
  uint32_t xffNumTrustedHops() const override { return 0; }
  bool skipXffAppend() const override { return false; }
  const std::string& via() const override { return EMPTY_STRING; }
  Http::ForwardClientCertType forwardClientCert() override {
    return Http::ForwardClientCertType::Sanitize;
  }
  const std::vector<Http::ClientCertDetailsType>& setCurrentClientCertDetails() const override {
    return set_current_client_cert_details_;
  }
  const Network::Address::Instance& localAddress() override;
  const absl::optional<std::string>& userAgent() override { return user_agent_; }
  const Http::TracingConnectionManagerConfig* tracingConfig() override { return nullptr; }
  Http::ConnectionManagerListenerStats& listenerStats() override { return listener_->stats_; }
  bool proxy100Continue() const override { return false; }
  const Http::Http1Settings& http1Settings() const override { return http1_settings_; }
  bool shouldNormalizePath() const override { return true; }
  bool shouldMergeSlashes() const override { return true; }
  LocalReply::LocalReply* localReply() const override { return nullptr; };
  Http::Code request(absl::string_view path_and_query, absl::string_view method,
                     Http::HeaderMap& response_headers, std::string& body) override;
  void closeSocket();
  void addListenerToHandler(Network::ConnectionHandler* handler) override;
  Server::Instance& server() { return server_; }

private:
  /**
   * Individual admin handler including prefix, help text, and callback.
   */
  struct UrlHandler {
    const std::string prefix_;
    const std::string help_text_;
    const HandlerCb handler_;
    const bool removable_;
    const bool mutates_server_state_;
  };

  /**
   * Implementation of RouteConfigProvider that returns a static null route config.
   */
  struct NullRouteConfigProvider : public Router::RouteConfigProvider {
    NullRouteConfigProvider(TimeSource& time_source);

    // Router::RouteConfigProvider
    Router::ConfigConstSharedPtr config() override { return config_; }
    absl::optional<ConfigInfo> configInfo() const override { return {}; }
    SystemTime lastUpdated() const override { return time_source_.systemTime(); }
    void onConfigUpdate() override {}
    void validateConfig(const envoy::api::v2::RouteConfiguration&) const override {}

    Router::ConfigConstSharedPtr config_;
    TimeSource& time_source_;
  };

  /**
   * Implementation of ScopedRouteConfigProvider that returns a null scoped route config.
   */
  struct NullScopedRouteConfigProvider : public Config::ConfigProvider {
    NullScopedRouteConfigProvider(TimeSource& time_source)
        : config_(std::make_shared<const Router::NullScopedConfigImpl>()),
          time_source_(time_source) {}

    ~NullScopedRouteConfigProvider() override = default;

    // Config::ConfigProvider
    SystemTime lastUpdated() const override { return time_source_.systemTime(); }
    const Protobuf::Message* getConfigProto() const override { return nullptr; }
    std::string getConfigVersion() const override { return ""; }
    ConfigConstSharedPtr getConfig() const override { return config_; }
    ApiType apiType() const override { return ApiType::Full; }
    ConfigProtoVector getConfigProtos() const override { return {}; }

    Router::ScopedConfigConstSharedPtr config_;
    TimeSource& time_source_;
  };

  friend class AdminStatsTest;

  /**
   * Attempt to change the log level of a logger or all loggers
   * @param params supplies the incoming endpoint query params.
   * @return TRUE if level change succeeded, FALSE otherwise.
   */
  bool changeLogLevel(const Http::Utility::QueryParams& params);

  /**
   * Helper methods for the /clusters url handler.
   */
  void addCircuitSettings(const std::string& cluster_name, const std::string& priority_str,
                          Upstream::ResourceManager& resource_manager, Buffer::Instance& response);
  void addOutlierInfo(const std::string& cluster_name,
                      const Upstream::Outlier::Detector* outlier_detector,
                      Buffer::Instance& response);
  void writeClustersAsJson(Buffer::Instance& response);
  void writeClustersAsText(Buffer::Instance& response);

  /**
   * Helper methods for the /listeners url handler.
   */
  void writeListenersAsJson(Buffer::Instance& response);
  void writeListenersAsText(Buffer::Instance& response);

  template <class StatType>
  static bool shouldShowMetric(const StatType& metric, const bool used_only,
                               const absl::optional<std::regex>& regex) {
    return ((!used_only || metric.used()) &&
            (!regex.has_value() || std::regex_search(metric.name(), regex.value())));
  }
  static std::string statsAsJson(const std::map<std::string, uint64_t>& all_stats,
                                 const std::vector<Stats::ParentHistogramSharedPtr>& all_histograms,
                                 bool used_only,
                                 const absl::optional<std::regex> regex = absl::nullopt,
                                 bool pretty_print = false);

  std::vector<const UrlHandler*> sortedHandlers() const;
  envoy::admin::v2alpha::ServerInfo::State serverState();
  /**
   * URL handlers.
   */
  Http::Code handlerAdminHome(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                              Buffer::Instance& response, AdminStream&);
  Http::Code handlerCerts(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                          Buffer::Instance& response, AdminStream&);
  Http::Code handlerClusters(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                             Buffer::Instance& response, AdminStream&);
  Http::Code handlerConfigDump(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&) const;
  Http::Code handlerContention(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&);
  Http::Code handlerCpuProfiler(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                                Buffer::Instance& response, AdminStream&);
  Http::Code handlerHeapProfiler(absl::string_view path_and_query,
                                 Http::HeaderMap& response_headers, Buffer::Instance& response,
                                 AdminStream&);
  Http::Code handlerHealthcheckFail(absl::string_view path_and_query,
                                    Http::HeaderMap& response_headers, Buffer::Instance& response,
                                    AdminStream&);
  Http::Code handlerHealthcheckOk(absl::string_view path_and_query,
                                  Http::HeaderMap& response_headers, Buffer::Instance& response,
                                  AdminStream&);
  Http::Code handlerHelp(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                         Buffer::Instance& response, AdminStream&);
  Http::Code handlerHotRestartVersion(absl::string_view path_and_query,
                                      Http::HeaderMap& response_headers, Buffer::Instance& response,
                                      AdminStream&);
  Http::Code handlerListenerInfo(absl::string_view path_and_query,
                                 Http::HeaderMap& response_headers, Buffer::Instance& response,
                                 AdminStream&);
  Http::Code handlerLogging(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                            Buffer::Instance& response, AdminStream&);
  Http::Code handlerMemory(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                           Buffer::Instance& response, AdminStream&);
  Http::Code handlerMain(const std::string& path, Buffer::Instance& response, AdminStream&);
  Http::Code handlerQuitQuitQuit(absl::string_view path_and_query,
                                 Http::HeaderMap& response_headers, Buffer::Instance& response,
                                 AdminStream&);
  Http::Code handlerDrainListeners(absl::string_view path_and_query,
                                   Http::HeaderMap& response_headers, Buffer::Instance& response,
                                   AdminStream&);
  Http::Code handlerResetCounters(absl::string_view path_and_query,
                                  Http::HeaderMap& response_headers, Buffer::Instance& response,
                                  AdminStream&);
  Http::Code handlerStatsRecentLookups(absl::string_view path_and_query,
                                       Http::HeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsClear(absl::string_view path_and_query,
                                            Http::HeaderMap& response_headers,
                                            Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsDisable(absl::string_view path_and_query,
                                              Http::HeaderMap& response_headers,
                                              Buffer::Instance& response, AdminStream&);
  Http::Code handlerStatsRecentLookupsEnable(absl::string_view path_and_query,
                                             Http::HeaderMap& response_headers,
                                             Buffer::Instance& response, AdminStream&);
  Http::Code handlerServerInfo(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream&);
  Http::Code handlerReady(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                          Buffer::Instance& response, AdminStream&);
  Http::Code handlerStats(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                          Buffer::Instance& response, AdminStream&);
  Http::Code handlerPrometheusStats(absl::string_view path_and_query,
                                    Http::HeaderMap& response_headers, Buffer::Instance& response,
                                    AdminStream&);
  Http::Code handlerRuntime(absl::string_view path_and_query, Http::HeaderMap& response_headers,
                            Buffer::Instance& response, AdminStream&);
  Http::Code handlerRuntimeModify(absl::string_view path_and_query,
                                  Http::HeaderMap& response_headers, Buffer::Instance& response,
                                  AdminStream&);
  bool isFormUrlEncoded(const Http::HeaderEntry* content_type) const;

  class AdminListener : public Network::ListenerConfig {
  public:
    AdminListener(AdminImpl& parent, Stats::ScopePtr&& listener_scope)
        : parent_(parent), name_("admin"), scope_(std::move(listener_scope)),
          stats_(Http::ConnectionManagerImpl::generateListenerStats("http.admin.", *scope_)) {}

    // Network::ListenerConfig
    Network::FilterChainManager& filterChainManager() override { return parent_; }
    Network::FilterChainFactory& filterChainFactory() override { return parent_; }
    Network::Socket& socket() override { return parent_.mutable_socket(); }
    const Network::Socket& socket() const override { return parent_.mutable_socket(); }
    bool bindToPort() override { return true; }
    bool handOffRestoredDestinationConnections() const override { return false; }
    uint32_t perConnectionBufferLimitBytes() const override { return 0; }
    std::chrono::milliseconds listenerFiltersTimeout() const override {
      return std::chrono::milliseconds();
    }
    bool continueOnListenerFiltersTimeout() const override { return false; }
    Stats::Scope& listenerScope() override { return *scope_; }
    uint64_t listenerTag() const override { return 0; }
    const std::string& name() const override { return name_; }
    const Network::ActiveUdpListenerFactory* udpListenerFactory() override {
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
    envoy::api::v2::core::TrafficDirection direction() const override {
      return envoy::api::v2::core::TrafficDirection::UNSPECIFIED;
    }
    Network::ConnectionBalancer& connectionBalancer() override { return connection_balancer_; }

    AdminImpl& parent_;
    const std::string name_;
    Stats::ScopePtr scope_;
    Http::ConnectionManagerListenerStats stats_;
    Network::NopConnectionBalancerImpl connection_balancer_;
  };
  using AdminListenerPtr = std::unique_ptr<AdminListener>;

  class AdminFilterChain : public Network::FilterChain {
  public:
    // We can't use the default constructor because transport_socket_factory_ doesn't have a
    // default constructor.
    AdminFilterChain() {} // NOLINT(modernize-use-equals-default)

    // Network::FilterChain
    const Network::TransportSocketFactory& transportSocketFactory() const override {
      return transport_socket_factory_;
    }

    const std::vector<Network::FilterFactoryCb>& networkFilterFactories() const override {
      return empty_network_filter_factory_;
    }

  private:
    const Network::RawBufferSocketFactory transport_socket_factory_;
    const std::vector<Network::FilterFactoryCb> empty_network_filter_factory_;
  };

  Server::Instance& server_;
  std::list<AccessLog::InstanceSharedPtr> access_logs_;
  const std::string profile_path_;
  Http::ConnectionManagerStats stats_;
  // Note: this is here to essentially blackhole the tracing stats since they aren't used in the
  // Admin case.
  Stats::IsolatedStoreImpl no_op_store_;
  Http::ConnectionManagerTracingStats tracing_stats_;
  NullRouteConfigProvider route_config_provider_;
  NullScopedRouteConfigProvider scoped_route_config_provider_;
  std::list<UrlHandler> handlers_;
  const uint32_t max_request_headers_kb_{Http::DEFAULT_MAX_REQUEST_HEADERS_KB};
  const uint32_t max_request_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  absl::optional<std::chrono::milliseconds> max_connection_duration_;
  absl::optional<std::string> user_agent_;
  Http::SlowDateProviderImpl date_provider_;
  std::vector<Http::ClientCertDetailsType> set_current_client_cert_details_;
  Http::Http1Settings http1_settings_;
  ConfigTrackerImpl config_tracker_;
  const Network::FilterChainSharedPtr admin_filter_chain_;
  Network::SocketPtr socket_;
  AdminListenerPtr listener_;
  const AdminInternalAddressConfig internal_address_config_;
};

/**
 * A terminal HTTP filter that implements server admin functionality.
 */
class AdminFilter : public Http::StreamDecoderFilter,
                    public AdminStream,
                    Logger::Loggable<Logger::Id::admin> {
public:
  AdminFilter(AdminImpl& parent);

  // Http::StreamFilterBase
  void onDestroy() override;

  // Http::StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::HeaderMap& headers, bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::HeaderMap& trailers) override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

  // AdminStream
  void setEndStreamOnComplete(bool end_stream) override { end_stream_on_complete_ = end_stream; }
  void addOnDestroyCallback(std::function<void()> cb) override;
  Http::StreamDecoderFilterCallbacks& getDecoderFilterCallbacks() const override;
  const Buffer::Instance* getRequestBody() const override;
  const Http::HeaderMap& getRequestHeaders() const override;

private:
  /**
   * Called when an admin request has been completely received.
   */
  void onComplete();

  AdminImpl& parent_;
  // Handlers relying on the reference should use addOnDestroyCallback()
  // to add a callback that will notify them when the reference is no
  // longer valid.
  Http::StreamDecoderFilterCallbacks* callbacks_{};
  Http::HeaderMap* request_headers_{};
  std::list<std::function<void()>> on_destroy_callbacks_;
  bool end_stream_on_complete_ = true;
};

/**
 * Formatter for metric/labels exported to Prometheus.
 *
 * See: https://prometheus.io/docs/concepts/data_model
 */
class PrometheusStatsFormatter {
public:
  /**
   * Extracts counters and gauges and relevant tags, appending them to
   * the response buffer after sanitizing the metric / label names.
   * @return uint64_t total number of metric types inserted in response.
   */
  static uint64_t statsAsPrometheus(const std::vector<Stats::CounterSharedPtr>& counters,
                                    const std::vector<Stats::GaugeSharedPtr>& gauges,
                                    const std::vector<Stats::ParentHistogramSharedPtr>& histograms,
                                    Buffer::Instance& response, const bool used_only,
                                    const absl::optional<std::regex>& regex);
  /**
   * Format the given tags, returning a string as a comma-separated list
   * of <tag_name>="<tag_value>" pairs.
   */
  static std::string formattedTags(const std::vector<Stats::Tag>& tags);
  /**
   * Format the given metric name, prefixed with "envoy_".
   */
  static std::string metricName(const std::string& extracted_name);

private:
  /**
   * Take a string and sanitize it according to Prometheus conventions.
   */
  static std::string sanitizeName(const std::string& name);

  /*
   * Determine whether a metric has never been emitted and choose to
   * not show it if we only wanted used metrics.
   */
  template <class StatType>
  static bool shouldShowMetric(const StatType& metric, const bool used_only,
                               const absl::optional<std::regex>& regex) {
    return ((!used_only || metric.used()) &&
            (!regex.has_value() || std::regex_search(metric.name(), regex.value())));
  }
};

} // namespace Server
} // namespace Envoy
