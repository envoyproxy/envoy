#pragma once

#include <chrono>
#include <functional>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include "envoy/http/filter.h"
#include "envoy/http/request_id_extension.h"
#include "envoy/network/filter.h"
#include "envoy/network/listen_socket.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"
#include "envoy/server/listener_manager.h"
#include "envoy/server/overload/overload_manager.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/resource_manager.h"

#include "source/common/common/assert.h"
#include "source/common/common/basic_resource_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/logger.h"
#include "source/common/common/macros.h"
#include "source/common/http/conn_manager_config.h"
#include "source/common/http/conn_manager_impl.h"
#include "source/common/http/date_provider_impl.h"
#include "source/common/http/default_server_string.h"
#include "source/common/http/http1/codec_stats.h"
#include "source/common/http/http2/codec_stats.h"
#include "source/common/http/request_id_extension_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/connection_balancer_impl.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/router/scoped_config_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/server/admin/admin_filter.h"
#include "source/server/admin/clusters_handler.h"
#include "source/server/admin/config_dump_handler.h"
#include "source/server/admin/config_tracker_impl.h"
#include "source/server/admin/init_dump_handler.h"
#include "source/server/admin/listeners_handler.h"
#include "source/server/admin/logs_handler.h"
#include "source/server/admin/profiling_handler.h"
#include "source/server/admin/runtime_handler.h"
#include "source/server/admin/server_cmd_handler.h"
#include "source/server/admin/server_info_handler.h"
#include "source/server/admin/stats_handler.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

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

  Http::Code runCallback(absl::string_view path_and_query,
                         Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                         AdminStream& admin_stream);
  const Network::Socket& socket() override { return *socket_; }
  Network::Socket& mutableSocket() { return *socket_; }

  // Server::Admin
  // TODO(jsedgwick) These can be managed with a generic version of ConfigTracker.
  // Wins would be no manual removeHandler() and code reuse.
  //
  // The prefix must start with "/" and contain at least one additional character.
  bool addHandler(const std::string& prefix, const std::string& help_text, HandlerCb callback,
                  bool removable, bool mutates_server_state) override;
  bool removeHandler(const std::string& prefix) override;
  ConfigTracker& getConfigTracker() override;

  void startHttpListener(const std::list<AccessLog::InstanceSharedPtr>& access_logs,
                         const std::string& address_out_path,
                         Network::Address::InstanceConstSharedPtr address,
                         const Network::Socket::OptionsSharedPtr& socket_options,
                         Stats::ScopePtr&& listener_scope) override;
  uint32_t concurrency() const override { return server_.options().concurrency(); }

  // Network::FilterChainManager
  const Network::FilterChain* findFilterChain(const Network::ConnectionSocket&) const override {
    return admin_filter_chain_.get();
  }

  // Network::FilterChainFactory
  bool
  createNetworkFilterChain(Network::Connection& connection,
                           const std::vector<Network::FilterFactoryCb>& filter_factories) override;
  bool createListenerFilterChain(Network::ListenerFilterManager&) override { return true; }
  void createUdpListenerFilterChain(Network::UdpListenerFilterManager&,
                                    Network::UdpReadFilterCallbacks&) override {}

  // Http::FilterChainFactory
  void createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) override;
  bool createUpgradeFilterChain(absl::string_view, const Http::FilterChainFactory::UpgradeMap*,
                                Http::FilterChainFactoryCallbacks&) override {
    return false;
  }

  // Http::ConnectionManagerConfig
  const Http::RequestIDExtensionSharedPtr& requestIDExtension() override {
    return request_id_extension_;
  }
  const std::list<AccessLog::InstanceSharedPtr>& accessLogs() override { return access_logs_; }
  Http::ServerConnectionPtr createCodec(Network::Connection& connection,
                                        const Buffer::Instance& data,
                                        Http::ServerConnectionCallbacks& callbacks) override;
  Http::DateProvider& dateProvider() override { return date_provider_; }
  std::chrono::milliseconds drainTimeout() const override { return std::chrono::milliseconds(100); }
  Http::FilterChainFactory& filterFactory() override { return *this; }
  bool generateRequestId() const override { return false; }
  bool preserveExternalRequestId() const override { return false; }
  bool alwaysSetRequestIdInResponse() const override { return false; }
  absl::optional<std::chrono::milliseconds> idleTimeout() const override { return idle_timeout_; }
  bool isRoutable() const override { return false; }
  absl::optional<std::chrono::milliseconds> maxConnectionDuration() const override {
    return max_connection_duration_;
  }
  uint32_t maxRequestHeadersKb() const override { return max_request_headers_kb_; }
  uint32_t maxRequestHeadersCount() const override { return max_request_headers_count_; }
  std::chrono::milliseconds streamIdleTimeout() const override { return {}; }
  std::chrono::milliseconds requestTimeout() const override { return {}; }
  std::chrono::milliseconds requestHeadersTimeout() const override { return {}; }
  std::chrono::milliseconds delayedCloseTimeout() const override { return {}; }
  absl::optional<std::chrono::milliseconds> maxStreamDuration() const override {
    return max_stream_duration_;
  }
  Router::RouteConfigProvider* routeConfigProvider() override { return &route_config_provider_; }
  Config::ConfigProvider* scopedRouteConfigProvider() override {
    return &scoped_route_config_provider_;
  }
  const std::string& serverName() const override { return Http::DefaultServerString::get(); }
  const absl::optional<std::string>& schemeToSet() const override { return scheme_; }
  HttpConnectionManagerProto::ServerHeaderTransformation
  serverHeaderTransformation() const override {
    return HttpConnectionManagerProto::OVERWRITE;
  }
  Http::ConnectionManagerStats& stats() override { return stats_; }
  Http::ConnectionManagerTracingStats& tracingStats() override { return tracing_stats_; }
  bool useRemoteAddress() const override { return true; }
  const Http::InternalAddressConfig& internalAddressConfig() const override {
    return internal_address_config_;
  }
  uint32_t xffNumTrustedHops() const override { return 0; }
  bool skipXffAppend() const override { return false; }
  const std::string& via() const override { return EMPTY_STRING; }
  Http::ForwardClientCertType forwardClientCert() const override {
    return Http::ForwardClientCertType::Sanitize;
  }
  const std::vector<Http::ClientCertDetailsType>& setCurrentClientCertDetails() const override {
    return set_current_client_cert_details_;
  }
  const Network::Address::Instance& localAddress() override;
  const absl::optional<std::string>& userAgent() override { return user_agent_; }
  Tracing::HttpTracerSharedPtr tracer() override { return nullptr; }
  const Http::TracingConnectionManagerConfig* tracingConfig() override { return nullptr; }
  Http::ConnectionManagerListenerStats& listenerStats() override { return listener_->stats_; }
  bool proxy100Continue() const override { return false; }
  bool streamErrorOnInvalidHttpMessaging() const override { return false; }
  const Http::Http1Settings& http1Settings() const override { return http1_settings_; }
  bool shouldNormalizePath() const override { return true; }
  bool shouldMergeSlashes() const override { return true; }
  bool shouldStripTrailingHostDot() const override { return false; }
  Http::StripPortType stripPortType() const override { return Http::StripPortType::None; }
  envoy::config::core::v3::HttpProtocolOptions::HeadersWithUnderscoresAction
  headersWithUnderscoresAction() const override {
    return envoy::config::core::v3::HttpProtocolOptions::ALLOW;
  }
  const LocalReply::LocalReply& localReply() const override { return *local_reply_; }
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      PathWithEscapedSlashesAction
      pathWithEscapedSlashesAction() const override {
    return envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
        KEEP_UNCHANGED;
  }
  const std::vector<Http::OriginalIPDetectionSharedPtr>&
  originalIpDetectionExtensions() const override {
    return detection_extensions_;
  }
  Http::Code request(absl::string_view path_and_query, absl::string_view method,
                     Http::ResponseHeaderMap& response_headers, std::string& body) override;
  void closeSocket();
  void addListenerToHandler(Network::ConnectionHandler* handler) override;
  Server::Instance& server() { return server_; }

  AdminFilter::AdminServerCallbackFunction createCallbackFunction() {
    return [this](absl::string_view path_and_query, Http::ResponseHeaderMap& response_headers,
                  Buffer::OwnedImpl& response, AdminFilter& filter) -> Http::Code {
      return runCallback(path_and_query, response_headers, response, filter);
    };
  }
  uint64_t maxRequestsPerConnection() const override { return 0; }

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
    void validateConfig(const envoy::config::route::v3::RouteConfiguration&) const override {}
    void requestVirtualHostsUpdate(const std::string&, Event::Dispatcher&,
                                   std::weak_ptr<Http::RouteConfigUpdatedCallback>) override {
      NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
    }

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

  /**
   * Implementation of OverloadManager that is never overloaded. Using this instead of the real
   * OverloadManager keeps the admin interface accessible even when the proxy is overloaded.
   */
  struct NullOverloadManager : public OverloadManager {
    struct NullThreadLocalOverloadState : public ThreadLocalOverloadState {
      NullThreadLocalOverloadState(Event::Dispatcher& dispatcher) : dispatcher_(dispatcher) {}
      const OverloadActionState& getState(const std::string&) override { return inactive_; }
      Event::Dispatcher& dispatcher_;
      const OverloadActionState inactive_ = OverloadActionState::inactive();
    };

    NullOverloadManager(ThreadLocal::SlotAllocator& slot_allocator)
        : tls_(slot_allocator.allocateSlot()) {}

    void start() override {
      tls_->set([](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
        return std::make_shared<NullThreadLocalOverloadState>(dispatcher);
      });
    }

    ThreadLocalOverloadState& getThreadLocalOverloadState() override {
      return tls_->getTyped<NullThreadLocalOverloadState>();
    }

    Event::ScaledRangeTimerManagerFactory scaledTimerFactory() override { return nullptr; }

    bool registerForAction(const std::string&, Event::Dispatcher&, OverloadActionCb) override {
      // This method shouldn't be called by the admin listener
      NOT_REACHED_GCOVR_EXCL_LINE;
      return false;
    }

    ThreadLocal::SlotPtr tls_;
  };

  std::vector<const UrlHandler*> sortedHandlers() const;
  envoy::admin::v3::ServerInfo::State serverState();

  /**
   * URL handlers.
   */
  Http::Code handlerAdminHome(absl::string_view path_and_query,
                              Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                              AdminStream&);

  Http::Code handlerHelp(absl::string_view path_and_query,
                         Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                         AdminStream&);

  class AdminListenSocketFactory : public Network::ListenSocketFactory {
  public:
    AdminListenSocketFactory(Network::SocketSharedPtr socket) : socket_(socket) {}

    // Network::ListenSocketFactory
    Network::Socket::Type socketType() const override { return socket_->socketType(); }
    const Network::Address::InstanceConstSharedPtr& localAddress() const override {
      return socket_->addressProvider().localAddress();
    }
    Network::SocketSharedPtr getListenSocket(uint32_t) override {
      // This is only supposed to be called once.
      RELEASE_ASSERT(!socket_create_, "AdminListener's socket shouldn't be shared.");
      socket_create_ = true;
      return socket_;
    }
    Network::ListenSocketFactoryPtr clone() const override { return nullptr; }
    void closeAllSockets() override {}
    void doFinalPreWorkerInit() override {}

  private:
    Network::SocketSharedPtr socket_;
    bool socket_create_{false};
  };

  class AdminListener : public Network::ListenerConfig {
  public:
    AdminListener(AdminImpl& parent, Stats::ScopePtr&& listener_scope)
        : parent_(parent), name_("admin"), scope_(std::move(listener_scope)),
          stats_(Http::ConnectionManagerImpl::generateListenerStats("http.admin.", *scope_)),
          init_manager_(nullptr) {}

    // Network::ListenerConfig
    Network::FilterChainManager& filterChainManager() override { return parent_; }
    Network::FilterChainFactory& filterChainFactory() override { return parent_; }
    Network::ListenSocketFactory& listenSocketFactory() override {
      return *parent_.socket_factory_;
    }
    bool bindToPort() override { return true; }
    bool handOffRestoredDestinationConnections() const override { return false; }
    uint32_t perConnectionBufferLimitBytes() const override { return 0; }
    std::chrono::milliseconds listenerFiltersTimeout() const override { return {}; }
    bool continueOnListenerFiltersTimeout() const override { return false; }
    Stats::Scope& listenerScope() override { return *scope_; }
    uint64_t listenerTag() const override { return 0; }
    const std::string& name() const override { return name_; }
    Network::UdpListenerConfigOptRef udpListenerConfig() override {
      return Network::UdpListenerConfigOptRef();
    }
    envoy::config::core::v3::TrafficDirection direction() const override {
      return envoy::config::core::v3::UNSPECIFIED;
    }
    Network::ConnectionBalancer& connectionBalancer() override { return connection_balancer_; }
    ResourceLimit& openConnections() override { return open_connections_; }
    const std::vector<AccessLog::InstanceSharedPtr>& accessLogs() const override {
      return empty_access_logs_;
    }
    uint32_t tcpBacklogSize() const override { return ENVOY_TCP_BACKLOG_SIZE; }
    Init::Manager& initManager() override { return *init_manager_; }

    AdminImpl& parent_;
    const std::string name_;
    Stats::ScopePtr scope_;
    Http::ConnectionManagerListenerStats stats_;
    Network::NopConnectionBalancerImpl connection_balancer_;
    BasicResourceLimitImpl open_connections_;

  private:
    const std::vector<AccessLog::InstanceSharedPtr> empty_access_logs_;
    std::unique_ptr<Init::Manager> init_manager_;
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

    std::chrono::milliseconds transportSocketConnectTimeout() const override {
      return std::chrono::milliseconds::zero();
    }

    const std::vector<Network::FilterFactoryCb>& networkFilterFactories() const override {
      return empty_network_filter_factory_;
    }

    absl::string_view name() const override { return "admin"; }

  private:
    const Network::RawBufferSocketFactory transport_socket_factory_;
    const std::vector<Network::FilterFactoryCb> empty_network_filter_factory_;
  };

  Server::Instance& server_;
  Http::RequestIDExtensionSharedPtr request_id_extension_;
  std::list<AccessLog::InstanceSharedPtr> access_logs_;
  const std::string profile_path_;
  Http::ConnectionManagerStats stats_;
  NullOverloadManager null_overload_manager_;
  // Note: this is here to essentially blackhole the tracing stats since they aren't used in the
  // Admin case.
  Stats::IsolatedStoreImpl no_op_store_;
  Http::ConnectionManagerTracingStats tracing_stats_;
  NullRouteConfigProvider route_config_provider_;
  NullScopedRouteConfigProvider scoped_route_config_provider_;
  Server::ClustersHandler clusters_handler_;
  Server::ConfigDumpHandler config_dump_handler_;
  Server::InitDumpHandler init_dump_handler_;
  Server::StatsHandler stats_handler_;
  Server::LogsHandler logs_handler_;
  Server::ProfilingHandler profiling_handler_;
  Server::RuntimeHandler runtime_handler_;
  Server::ListenersHandler listeners_handler_;
  Server::ServerCmdHandler server_cmd_handler_;
  Server::ServerInfoHandler server_info_handler_;
  std::list<UrlHandler> handlers_;
  const uint32_t max_request_headers_kb_{Http::DEFAULT_MAX_REQUEST_HEADERS_KB};
  const uint32_t max_request_headers_count_{Http::DEFAULT_MAX_HEADERS_COUNT};
  absl::optional<std::chrono::milliseconds> idle_timeout_;
  absl::optional<std::chrono::milliseconds> max_connection_duration_;
  absl::optional<std::chrono::milliseconds> max_stream_duration_;
  absl::optional<std::string> user_agent_;
  Http::SlowDateProviderImpl date_provider_;
  std::vector<Http::ClientCertDetailsType> set_current_client_cert_details_;
  Http::Http1Settings http1_settings_;
  Http::Http1::CodecStats::AtomicPtr http1_codec_stats_;
  Http::Http2::CodecStats::AtomicPtr http2_codec_stats_;
  ConfigTrackerImpl config_tracker_;
  const Network::FilterChainSharedPtr admin_filter_chain_;
  Network::SocketSharedPtr socket_;
  Network::ListenSocketFactoryPtr socket_factory_;
  AdminListenerPtr listener_;
  const AdminInternalAddressConfig internal_address_config_;
  const LocalReply::LocalReplyPtr local_reply_;
  const std::vector<Http::OriginalIPDetectionSharedPtr> detection_extensions_{};
  const absl::optional<std::string> scheme_{};
};

} // namespace Server
} // namespace Envoy
