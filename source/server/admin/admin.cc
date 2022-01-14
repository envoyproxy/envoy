#include "source/server/admin/admin.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "envoy/filesystem/filesystem.h"
#include "envoy/server/hot_restart.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/outlier_detection.h"
#include "envoy/upstream/upstream.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/mutex_tracer_impl.h"
#include "source/common/common/utility.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/codes.h"
#include "source/common/http/conn_manager_utility.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/memory/utils.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/extensions/request_id/uuid/config.h"
#include "source/server/admin/admin_html_generator.h"
#include "source/server/admin/utils.h"
#include "source/server/listener_impl.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

ConfigTracker& AdminImpl::getConfigTracker() { return config_tracker_; }

AdminImpl::NullRouteConfigProvider::NullRouteConfigProvider(TimeSource& time_source)
    : config_(new Router::NullConfigImpl()), time_source_(time_source) {}

void AdminImpl::startHttpListener(const std::list<AccessLog::InstanceSharedPtr>& access_logs,
                                  const std::string& address_out_path,
                                  Network::Address::InstanceConstSharedPtr address,
                                  const Network::Socket::OptionsSharedPtr& socket_options,
                                  Stats::ScopePtr&& listener_scope) {
  for (const auto& access_log : access_logs) {
    access_logs_.emplace_back(access_log);
  }
  null_overload_manager_.start();
  socket_ = std::make_shared<Network::TcpListenSocket>(address, socket_options, true);
  RELEASE_ASSERT(0 == socket_->ioHandle().listen(ENVOY_TCP_BACKLOG_SIZE).return_value_,
                 "listen() failed on admin listener");
  socket_factory_ = std::make_unique<AdminListenSocketFactory>(socket_);
  listener_ = std::make_unique<AdminListener>(*this, std::move(listener_scope));
  ENVOY_LOG(info, "admin address: {}",
            socket().connectionInfoProvider().localAddress()->asString());
  if (!address_out_path.empty()) {
    std::ofstream address_out_file(address_out_path);
    if (!address_out_file) {
      ENVOY_LOG(critical, "cannot open admin address output file {} for writing.",
                address_out_path);
    } else {
      address_out_file << socket_->connectionInfoProvider().localAddress()->asString();
    }
  }
}

AdminImpl::AdminImpl(const std::string& profile_path, Server::Instance& server,
                     bool ignore_global_conn_limit)
    : server_(server),
      request_id_extension_(Extensions::RequestId::UUIDRequestIDExtension::defaultInstance(
          server_.api().randomGenerator())),
      profile_path_(profile_path),
      stats_(Http::ConnectionManagerImpl::generateStats("http.admin.", server_.stats())),
      null_overload_manager_(server_.threadLocal()),
      tracing_stats_(
          Http::ConnectionManagerImpl::generateTracingStats("http.admin.", no_op_store_)),
      route_config_provider_(server.timeSource()),
      scoped_route_config_provider_(server.timeSource()), clusters_handler_(server),
      config_dump_handler_(config_tracker_, server), init_dump_handler_(server),
      stats_handler_(server), logs_handler_(server), profiling_handler_(profile_path),
      runtime_handler_(server), listeners_handler_(server), server_cmd_handler_(server),
      server_info_handler_(server),
      // TODO(jsedgwick) add /runtime_reset endpoint that removes all admin-set values
      handlers_{
          {"/", "Admin home page", MAKE_ADMIN_HANDLER(handlerAdminHome), false, false, {}},
          {"/certs",
           "print certs on machine",
           MAKE_ADMIN_HANDLER(server_info_handler_.handlerCerts),
           false,
           false,
           {}},
          {"/clusters",
           "upstream cluster status",
           MAKE_ADMIN_HANDLER(clusters_handler_.handlerClusters),
           false,
           false,
           {}},
          {"/config_dump",
           "dump current Envoy configs (experimental)",
           MAKE_ADMIN_HANDLER(config_dump_handler_.handlerConfigDump),
           false,
           false,
           {}},
          {"/init_dump",
           "dump current Envoy init manager information (experimental)",
           MAKE_ADMIN_HANDLER(init_dump_handler_.handlerInitDump),
           false,
           false,
           {}},
          {"/contention",
           "dump current Envoy mutex contention stats (if enabled)",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerContention),
           false,
           false,
           {}},
          {"/cpuprofiler",
           "enable/disable the CPU profiler",
           MAKE_ADMIN_HANDLER(profiling_handler_.handlerCpuProfiler),
           false,
           true,
           {}},
          {"/heapprofiler",
           "enable/disable the heap profiler",
           MAKE_ADMIN_HANDLER(profiling_handler_.handlerHeapProfiler),
           false,
           true,
           {}},
          {"/healthcheck/fail",
           "cause the server to fail health checks",
           MAKE_ADMIN_HANDLER(server_cmd_handler_.handlerHealthcheckFail),
           false,
           true,
           {}},
          {"/healthcheck/ok",
           "cause the server to pass health checks",
           MAKE_ADMIN_HANDLER(server_cmd_handler_.handlerHealthcheckOk),
           false,
           true,
           {}},
          {"/help",
           "print out list of admin commands",
           MAKE_ADMIN_HANDLER(handlerHelp),
           false,
           false,
           {}},
          {"/hot_restart_version",
           "print the hot restart compatibility version",
           MAKE_ADMIN_HANDLER(server_info_handler_.handlerHotRestartVersion),
           false,
           false,
           {}},
          {"/logging",
           "query/change logging levels",
           MAKE_ADMIN_HANDLER(logs_handler_.handlerLogging),
           false,
           true,
           {}},
          {"/memory",
           "print current allocation/heap usage",
           MAKE_ADMIN_HANDLER(server_info_handler_.handlerMemory),
           false,
           false,
           {}},
          {"/quitquitquit",
           "exit the server",
           MAKE_ADMIN_HANDLER(server_cmd_handler_.handlerQuitQuitQuit),
           false,
           true,
           {}},
          {"/reset_counters",
           "reset all counters to zero",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerResetCounters),
           false,
           true,
           {}},
          {"/drain_listeners",
           "drain listeners",
           MAKE_ADMIN_HANDLER(listeners_handler_.handlerDrainListeners),
           false,
           true,
           {}},
          {"/server_info",
           "print server version/status information",
           MAKE_ADMIN_HANDLER(server_info_handler_.handlerServerInfo),
           false,
           false,
           {}},
          {"/ready",
           "print server state, return 200 if LIVE, otherwise return 503",
           MAKE_ADMIN_HANDLER(server_info_handler_.handlerReady),
           false,
           false,
           {}},
          stats_handler_.statsHandler(),
          {"/stats/prometheus",
           "print server stats in prometheus format",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsPrometheus),
           false,
           false,
           {{ParamDescriptor::Type::Boolean, "usedonly",
             "Only include stats that have been written by system since restart"},
            {ParamDescriptor::Type::Boolean, "text_readouts",
             "Render text_readouts as new gaugues with value 0 (increases Prometheus data size)"},
            {ParamDescriptor::Type::String, "filter",
             "Regular expression (ecmascript) for filtering stats"}}},
          {"/stats/recentlookups",
           "Show recent stat-name lookups",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookups),
           false,
           false,
           {}},
          {"/stats/recentlookups/clear",
           "clear list of stat-name lookups and counter",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookupsClear),
           false,
           true,
           {}},
          {"/stats/recentlookups/disable",
           "disable recording of reset stat-name lookup names",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookupsDisable),
           false,
           true,
           {}},
          {"/stats/recentlookups/enable",
           "enable recording of reset stat-name lookup names",
           MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookupsEnable),
           false,
           true,
           {}},
          {"/listeners",
           "print listener info",
           MAKE_ADMIN_HANDLER(listeners_handler_.handlerListenerInfo),
           false,
           false,
           {}},
          {"/runtime",
           "print runtime values",
           MAKE_ADMIN_HANDLER(runtime_handler_.handlerRuntime),
           false,
           false,
           {}},
          {"/runtime_modify",
           "modify runtime values",
           MAKE_ADMIN_HANDLER(runtime_handler_.handlerRuntimeModify),
           false,
           true,
           {}},
          {"/reopen_logs",
           "reopen access logs",
           MAKE_ADMIN_HANDLER(logs_handler_.handlerReopenLogs),
           false,
           true,
           {}},
      },
      date_provider_(server.dispatcher().timeSource()),
      admin_filter_chain_(std::make_shared<AdminFilterChain>()),
      local_reply_(LocalReply::Factory::createDefault()),
      ignore_global_conn_limit_(ignore_global_conn_limit) {}

Http::ServerConnectionPtr AdminImpl::createCodec(Network::Connection& connection,
                                                 const Buffer::Instance& data,
                                                 Http::ServerConnectionCallbacks& callbacks) {
  return Http::ConnectionManagerUtility::autoCreateCodec(
      connection, data, callbacks, server_.stats(), server_.api().randomGenerator(),
      http1_codec_stats_, http2_codec_stats_, Http::Http1Settings(),
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions()),
      maxRequestHeadersKb(), maxRequestHeadersCount(), headersWithUnderscoresAction());
}

bool AdminImpl::createNetworkFilterChain(Network::Connection& connection,
                                         const std::vector<Network::FilterFactoryCb>&) {
  // Pass in the null overload manager so that the admin interface is accessible even when Envoy
  // is overloaded.
  connection.addReadFilter(Network::ReadFilterSharedPtr{new Http::ConnectionManagerImpl(
      *this, server_.drainManager(), server_.api().randomGenerator(), server_.httpContext(),
      server_.runtime(), server_.localInfo(), server_.clusterManager(), null_overload_manager_,
      server_.timeSource())});
  return true;
}

void AdminImpl::createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) {
  callbacks.addStreamFilter(std::make_shared<AdminFilter>(createCallbackFunction()));
}

Http::Code AdminImpl::runCallback(absl::string_view path_and_query,
                                  Http::ResponseHeaderMap& response_headers,
                                  Buffer::Instance& response, AdminStream& admin_stream) {

  Http::Code code = Http::Code::OK;
  bool found_handler = false;

  std::string::size_type query_index = path_and_query.find('?');
  if (query_index == std::string::npos) {
    query_index = path_and_query.size();
  }

  for (const UrlHandler& handler : handlers_) {
    if (path_and_query.compare(0, query_index, handler.prefix_) == 0) {
      found_handler = true;
      if (handler.mutates_server_state_) {
        const absl::string_view method = admin_stream.getRequestHeaders().getMethodValue();
        if (method != Http::Headers::get().MethodValues.Post) {
          ENVOY_LOG(error, "admin path \"{}\" mutates state, method={} rather than POST",
                    handler.prefix_, method);
          code = Http::Code::MethodNotAllowed;
          response.add(fmt::format("Method {} not allowed, POST required.", method));
          break;
        }
      }
      code = handler.handler_(path_and_query, response_headers, response, admin_stream);
      Memory::Utils::tryShrinkHeap();
      break;
    }
  }

  if (!found_handler) {
    // Extra space is emitted below to have "invalid path." be a separate sentence in the
    // 404 output from "admin commands are:" in handlerHelp.
    response.add("invalid path. ");
    handlerHelp(path_and_query, response_headers, response, admin_stream);
    code = Http::Code::NotFound;
  }

  return code;
}

std::vector<const AdminImpl::UrlHandler*> AdminImpl::sortedHandlers() const {
  std::vector<const UrlHandler*> sorted_handlers;
  for (const UrlHandler& handler : handlers_) {
    sorted_handlers.push_back(&handler);
  }
  // Note: it's generally faster to sort a vector with std::vector than to construct a std::map.
  std::sort(sorted_handlers.begin(), sorted_handlers.end(),
            [](const UrlHandler* h1, const UrlHandler* h2) { return h1->prefix_ < h2->prefix_; });
  return sorted_handlers;
}

Http::Code AdminImpl::handlerHelp(absl::string_view, Http::ResponseHeaderMap&,
                                  Buffer::Instance& response, AdminStream&) {
  response.add("admin commands are:\n");

  // Prefix order is used during searching, but for printing do them in alpha order.
  for (const UrlHandler* handler : sortedHandlers()) {
    response.add(fmt::format("  {}: {}\n", handler->prefix_, handler->help_text_));
  }
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerAdminHome(absl::string_view, Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&) {
  response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Html);
  AdminHtmlGenerator html(response);
  html.renderHead();
  html.renderTableBegin();

  // Prefix order is used during searching, but for printing do them in alpha order.
  OptRef<const Http::Utility::QueryParams> no_query_params;
  for (const UrlHandler* handler : sortedHandlers()) {
    html.renderUrlHandler(*handler, no_query_params);
  }

  html.renderTableEnd();

  // gen.renderTail();
  return Http::Code::OK;
}

const Network::Address::Instance& AdminImpl::localAddress() {
  return *server_.localInfo().address();
}

bool AdminImpl::addHandler(const std::string& prefix, const std::string& help_text,
                           HandlerCb callback, bool removable, bool mutates_state,
                           const ParamDescriptorVec& params) {
  ASSERT(prefix.size() > 1);
  ASSERT(prefix[0] == '/');

  // Sanitize prefix and help_text to ensure no XSS can be injected, as
  // we are injecting these strings into HTML that runs in a domain that
  // can mutate Envoy server state. Also rule out some characters that
  // make no sense as part of a URL path: ? and :.
  const std::string::size_type pos = prefix.find_first_of("&\"'<>?:");
  if (pos != std::string::npos) {
    ENVOY_LOG(error, "filter \"{}\" contains invalid character '{}'", prefix, prefix[pos]);
    return false;
  }

  auto it = std::find_if(handlers_.cbegin(), handlers_.cend(),
                         [&prefix](const UrlHandler& entry) { return prefix == entry.prefix_; });
  if (it == handlers_.end()) {
    handlers_.push_back({prefix, help_text, callback, removable, mutates_state, params});
    return true;
  }
  return false;
}

bool AdminImpl::removeHandler(const std::string& prefix) {
  const size_t size_before_removal = handlers_.size();
  handlers_.remove_if(
      [&prefix](const UrlHandler& entry) { return prefix == entry.prefix_ && entry.removable_; });
  if (handlers_.size() != size_before_removal) {
    return true;
  }
  return false;
}

Http::Code AdminImpl::request(absl::string_view path_and_query, absl::string_view method,
                              Http::ResponseHeaderMap& response_headers, std::string& body) {
  AdminFilter filter(createCallbackFunction());

  auto request_headers = Http::RequestHeaderMapImpl::create();
  request_headers->setMethod(method);
  filter.decodeHeaders(*request_headers, false);
  Buffer::OwnedImpl response;

  Http::Code code = runCallback(path_and_query, response_headers, response, filter);
  Utility::populateFallbackResponseHeaders(code, response_headers);
  body = response.toString();
  return code;
}

void AdminImpl::closeSocket() {
  if (socket_) {
    socket_->close();
  }
}

void AdminImpl::addListenerToHandler(Network::ConnectionHandler* handler) {
  if (listener_) {
    handler->addListener(absl::nullopt, *listener_);
  }
}

} // namespace Server
} // namespace Envoy
