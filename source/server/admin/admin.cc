#include "source/server/admin/admin.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/http/header_validator_factory.h"
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
#include "source/common/listener_manager/listener_impl.h"
#include "source/common/memory/utils.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/router/config_impl.h"
#include "source/extensions/request_id/uuid/config.h"
#include "source/server/admin/utils.h"

#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "spdlog/spdlog.h"

namespace Envoy {
namespace Server {

ConfigTracker& AdminImpl::getConfigTracker() { return config_tracker_; }

AdminImpl::NullRouteConfigProvider::NullRouteConfigProvider(TimeSource& time_source)
    : config_(new Router::NullConfigImpl()), time_source_(time_source) {}

void AdminImpl::startHttpListener(std::list<AccessLog::InstanceSharedPtr> access_logs,
                                  Network::Address::InstanceConstSharedPtr address,
                                  Network::Socket::OptionsSharedPtr socket_options) {
  access_logs_ = std::move(access_logs);

  null_overload_manager_.start();
  socket_ = std::make_shared<Network::TcpListenSocket>(address, socket_options, true);
  RELEASE_ASSERT(0 == socket_->ioHandle().listen(ENVOY_TCP_BACKLOG_SIZE).return_value_,
                 "listen() failed on admin listener");
  socket_factories_.emplace_back(std::make_unique<AdminListenSocketFactory>(socket_));
  listener_ = std::make_unique<AdminListener>(*this, factory_context_.listenerScope());

  ENVOY_LOG(info, "admin address: {}",
            socket().connectionInfoProvider().localAddress()->asString());

  if (!server_.options().adminAddressPath().empty()) {
    std::ofstream address_out_file(server_.options().adminAddressPath());
    if (!address_out_file) {
      ENVOY_LOG(critical, "cannot open admin address output file {} for writing.",
                server_.options().adminAddressPath());
    } else {
      address_out_file << socket_->connectionInfoProvider().localAddress()->asString();
    }
  }
}

namespace {
// Prepends an element to an array, modifying it as passed in.
std::vector<absl::string_view> prepend(const absl::string_view first,
                                       std::vector<absl::string_view> strings) {
  strings.insert(strings.begin(), first);
  return strings;
}

Http::HeaderValidatorFactoryPtr createHeaderValidatorFactory(
    [[maybe_unused]] Server::Configuration::ServerFactoryContext& context) {
  Http::HeaderValidatorFactoryPtr header_validator_factory;
#ifdef ENVOY_ENABLE_UHV
  // Default UHV config matches the admin HTTP validation and normalization config
  ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig uhv_config;

  ::envoy::config::core::v3::TypedExtensionConfig config;
  config.set_name("default_universal_header_validator_for_admin");
  config.mutable_typed_config()->PackFrom(uhv_config);

  auto* factory = Envoy::Config::Utility::getFactory<Http::HeaderValidatorFactoryConfig>(config);
  ENVOY_BUG(factory != nullptr, "Default UHV is not linked into binary.");

  header_validator_factory = factory->createFromProto(config.typed_config(), context);
  ENVOY_BUG(header_validator_factory != nullptr, "Unable to create default UHV.");
#endif
  return header_validator_factory;
}

} // namespace

AdminImpl::AdminImpl(const std::string& profile_path, Server::Instance& server,
                     bool ignore_global_conn_limit)
    : server_(server), factory_context_(server),
      request_id_extension_(Extensions::RequestId::UUIDRequestIDExtension::defaultInstance(
          server_.api().randomGenerator())),
      profile_path_(profile_path), stats_(Http::ConnectionManagerImpl::generateStats(
                                       "http.admin.", *server_.stats().rootScope())),
      null_overload_manager_(server_.threadLocal(), false),
      tracing_stats_(Http::ConnectionManagerImpl::generateTracingStats("http.admin.",
                                                                       *no_op_store_.rootScope())),
      route_config_provider_(server.timeSource()),
      scoped_route_config_provider_(server.timeSource()), clusters_handler_(server),
      config_dump_handler_(config_tracker_, server), init_dump_handler_(server),
      stats_handler_(server), logs_handler_(server), profiling_handler_(profile_path),
      runtime_handler_(server), listeners_handler_(server), server_cmd_handler_(server),
      server_info_handler_(server),
      // TODO(jsedgwick) add /runtime_reset endpoint that removes all admin-set values
      handlers_{
          makeHandler("/", "Admin home page", MAKE_ADMIN_HANDLER(handlerAdminHome), false, false),
          makeHandler("/certs", "print certs on machine",
                      MAKE_ADMIN_HANDLER(server_info_handler_.handlerCerts), false, false),
          makeHandler("/clusters", "upstream cluster status",
                      MAKE_ADMIN_HANDLER(clusters_handler_.handlerClusters), false, false),
          makeHandler(
              "/config_dump", "dump current Envoy configs (experimental)",
              MAKE_ADMIN_HANDLER(config_dump_handler_.handlerConfigDump), false, false,
              {{Admin::ParamDescriptor::Type::String, "resource", "The resource to dump"},
               {Admin::ParamDescriptor::Type::String, "mask",
                "The mask to apply. When both resource and mask are specified, "
                "the mask is applied to every element in the desired repeated field so that only a "
                "subset of fields are returned. The mask is parsed as a ProtobufWkt::FieldMask"},
               {Admin::ParamDescriptor::Type::String, "name_regex",
                "Dump only the currently loaded configurations whose names match the specified "
                "regex. Can be used with both resource and mask query parameters."},
               {Admin::ParamDescriptor::Type::Boolean, "include_eds",
                "Dump currently loaded configuration including EDS. See the response definition "
                "for more information"}}),
          makeHandler("/init_dump", "dump current Envoy init manager information (experimental)",
                      MAKE_ADMIN_HANDLER(init_dump_handler_.handlerInitDump), false, false,
                      {{Admin::ParamDescriptor::Type::String, "mask",
                        "The desired component to dump unready targets. The mask is parsed as "
                        "a ProtobufWkt::FieldMask. For example, get the unready targets of "
                        "all listeners with /init_dump?mask=listener`"}}),
          makeHandler("/contention", "dump current Envoy mutex contention stats (if enabled)",
                      MAKE_ADMIN_HANDLER(stats_handler_.handlerContention), false, false),
          makeHandler("/cpuprofiler", "enable/disable the CPU profiler",
                      MAKE_ADMIN_HANDLER(profiling_handler_.handlerCpuProfiler), false, true,
                      {{Admin::ParamDescriptor::Type::Enum,
                        "enable",
                        "enables the CPU profiler",
                        {"y", "n"}}}),
          makeHandler("/heapprofiler", "enable/disable the heap profiler",
                      MAKE_ADMIN_HANDLER(profiling_handler_.handlerHeapProfiler), false, true,
                      {{Admin::ParamDescriptor::Type::Enum,
                        "enable",
                        "enable/disable the heap profiler",
                        {"y", "n"}}}),
          makeHandler("/heap_dump", "dump current Envoy heap (if supported)",
                      MAKE_ADMIN_HANDLER(tcmalloc_profiling_handler_.handlerHeapDump), false,
                      false),
          makeHandler("/healthcheck/fail", "cause the server to fail health checks",
                      MAKE_ADMIN_HANDLER(server_cmd_handler_.handlerHealthcheckFail), false, true),
          makeHandler("/healthcheck/ok", "cause the server to pass health checks",
                      MAKE_ADMIN_HANDLER(server_cmd_handler_.handlerHealthcheckOk), false, true),
          makeHandler("/help", "print out list of admin commands", MAKE_ADMIN_HANDLER(handlerHelp),
                      false, false),
          makeHandler("/hot_restart_version", "print the hot restart compatibility version",
                      MAKE_ADMIN_HANDLER(server_info_handler_.handlerHotRestartVersion), false,
                      false),

          // The logging "level" parameter, if specified as a non-blank entry,
          // changes all the logging-paths to that level. So the enum parameter
          // needs to include a an empty string as the default (first) option.
          // Thus we prepend an empty string to the logging-levels list.
          makeHandler("/logging", "query/change logging levels",
                      MAKE_ADMIN_HANDLER(logs_handler_.handlerLogging), false, true,
                      {{Admin::ParamDescriptor::Type::String, "paths",
                        "Change multiple logging levels by setting to "
                        "<logger_name1>:<desired_level1>,<logger_name2>:<desired_level2>."},
                       {Admin::ParamDescriptor::Type::Enum, "level", "desired logging level",
                        prepend("", LogsHandler::levelStrings())}}),
          makeHandler("/memory", "print current allocation/heap usage",
                      MAKE_ADMIN_HANDLER(server_info_handler_.handlerMemory), false, false),
          makeHandler("/quitquitquit", "exit the server",
                      MAKE_ADMIN_HANDLER(server_cmd_handler_.handlerQuitQuitQuit), false, true),
          makeHandler("/reset_counters", "reset all counters to zero",
                      MAKE_ADMIN_HANDLER(stats_handler_.handlerResetCounters), false, true),
          makeHandler(
              "/drain_listeners", "drain listeners",
              MAKE_ADMIN_HANDLER(listeners_handler_.handlerDrainListeners), false, true,
              {{ParamDescriptor::Type::Boolean, "graceful",
                "When draining listeners, enter a graceful drain period prior to closing "
                "listeners. This behaviour and duration is configurable via server options "
                "or CLI"},
               {ParamDescriptor::Type::Boolean, "skip_exit",
                "When draining listeners, do not exit after the drain period. "
                "This must be used with graceful"},
               {ParamDescriptor::Type::Boolean, "inboundonly",
                "Drains all inbound listeners. traffic_direction field in "
                "envoy_v3_api_msg_config.listener.v3.Listener is used to determine whether a "
                "listener is inbound or outbound."}}),
          makeHandler("/server_info", "print server version/status information",
                      MAKE_ADMIN_HANDLER(server_info_handler_.handlerServerInfo), false, false),
          makeHandler("/ready", "print server state, return 200 if LIVE, otherwise return 503",
                      MAKE_ADMIN_HANDLER(server_info_handler_.handlerReady), false, false),
          stats_handler_.statsHandler(false /* not active mode */),
          makeHandler("/stats/prometheus", "print server stats in prometheus format",
                      MAKE_ADMIN_HANDLER(stats_handler_.handlerPrometheusStats), false, false,
                      {{ParamDescriptor::Type::Boolean, "usedonly",
                        "Only include stats that have been written by system since restart"},
                       {ParamDescriptor::Type::Boolean, "text_readouts",
                        "Render text_readouts as new gaugues with value 0 (increases Prometheus "
                        "data size)"},
                       {ParamDescriptor::Type::String, "filter",
                        "Regular expression (Google re2) for filtering stats"}}),
          makeHandler("/stats/recentlookups", "Show recent stat-name lookups",
                      MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookups), false, false),
          makeHandler("/stats/recentlookups/clear", "clear list of stat-name lookups and counter",
                      MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookupsClear), false,
                      true),
          makeHandler(
              "/stats/recentlookups/disable", "disable recording of reset stat-name lookup names",
              MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookupsDisable), false, true),
          makeHandler(
              "/stats/recentlookups/enable", "enable recording of reset stat-name lookup names",
              MAKE_ADMIN_HANDLER(stats_handler_.handlerStatsRecentLookupsEnable), false, true),
          makeHandler("/listeners", "print listener info",
                      MAKE_ADMIN_HANDLER(listeners_handler_.handlerListenerInfo), false, false,
                      {{Admin::ParamDescriptor::Type::Enum,
                        "format",
                        "File format to use",
                        {"text", "json"}}}),
          makeHandler("/runtime", "print runtime values",
                      MAKE_ADMIN_HANDLER(runtime_handler_.handlerRuntime), false, false),
          makeHandler("/runtime_modify",
                      "Adds or modifies runtime values as passed in query parameters. To delete a "
                      "previously added key, use an empty string as the value. Note that deletion "
                      "only applies to overrides added via this endpoint; values loaded from disk "
                      "can be modified via override but not deleted. E.g. "
                      "?key1=value1&key2=value2...",
                      MAKE_ADMIN_HANDLER(runtime_handler_.handlerRuntimeModify), false, true),
          makeHandler("/reopen_logs", "reopen access logs",
                      MAKE_ADMIN_HANDLER(logs_handler_.handlerReopenLogs), false, true),
      },
      date_provider_(server.dispatcher().timeSource()),
      admin_filter_chain_(std::make_shared<AdminFilterChain>()),
      local_reply_(LocalReply::Factory::createDefault()),
      ignore_global_conn_limit_(ignore_global_conn_limit),
      header_validator_factory_(createHeaderValidatorFactory(server.serverFactoryContext())) {
#ifndef NDEBUG
  // Verify that no duplicate handlers exist.
  absl::flat_hash_set<absl::string_view> handlers;
  for (const UrlHandler& handler : handlers_) {
    ASSERT(handlers.insert(handler.prefix_).second);
  }
#endif
}

Http::ServerConnectionPtr AdminImpl::createCodec(Network::Connection& connection,
                                                 const Buffer::Instance& data,
                                                 Http::ServerConnectionCallbacks& callbacks,
                                                 Server::OverloadManager& overload_manager) {
  return Http::ConnectionManagerUtility::autoCreateCodec(
      connection, data, callbacks, *server_.stats().rootScope(), server_.api().randomGenerator(),
      http1_codec_stats_, http2_codec_stats_, Http::Http1Settings(),
      ::Envoy::Http2::Utility::initializeAndValidateOptions(
          envoy::config::core::v3::Http2ProtocolOptions()),
      maxRequestHeadersKb(), maxRequestHeadersCount(), headersWithUnderscoresAction(),
      overload_manager);
}

bool AdminImpl::createNetworkFilterChain(Network::Connection& connection,
                                         const Filter::NetworkFilterFactoriesList&) {
  // Pass in the null overload manager so that the admin interface is accessible even when Envoy
  // is overloaded.
  connection.addReadFilter(Network::ReadFilterSharedPtr{new Http::ConnectionManagerImpl(
      *this, server_.drainManager(), server_.api().randomGenerator(), server_.httpContext(),
      server_.runtime(), server_.localInfo(), server_.clusterManager(), null_overload_manager_,
      server_.timeSource())});
  return true;
}

bool AdminImpl::createFilterChain(Http::FilterChainManager& manager, bool,
                                  const Http::FilterChainOptions&) const {
  Http::FilterFactoryCb factory = [this](Http::FilterChainFactoryCallbacks& callbacks) {
    callbacks.addStreamFilter(std::make_shared<AdminFilter>(createRequestFunction()));
  };
  manager.applyFilterFactoryCb({}, factory);
  return true;
}

namespace {
// Implements a chunked request for static text.
class StaticTextRequest : public Admin::Request {
public:
  StaticTextRequest(absl::string_view response_text, Http::Code code) : code_(code) {
    response_text_.add(response_text);
  }
  StaticTextRequest(Buffer::Instance& response_text, Http::Code code) : code_(code) {
    response_text_.move(response_text);
  }

  Http::Code start(Http::ResponseHeaderMap&) override { return code_; }
  bool nextChunk(Buffer::Instance& response) override {
    response.move(response_text_);
    return false;
  }

private:
  Buffer::OwnedImpl response_text_;
  const Http::Code code_;
};

// Implements a streaming Request based on a non-streaming callback that
// generates the entire admin output in one shot.
class RequestGasket : public Admin::Request {
public:
  RequestGasket(Admin::HandlerCb handler_cb, AdminStream& admin_stream)
      : handler_cb_(handler_cb), admin_stream_(admin_stream) {}

  static Admin::GenRequestFn makeGen(Admin::HandlerCb callback) {
    return [callback](AdminStream& admin_stream) -> Server::Admin::RequestPtr {
      return std::make_unique<RequestGasket>(callback, admin_stream);
    };
  }

  Http::Code start(Http::ResponseHeaderMap& response_headers) override {
    return handler_cb_(response_headers, response_, admin_stream_);
  }

  bool nextChunk(Buffer::Instance& response) override {
    response.move(response_);
    return false;
  }

private:
  Admin::HandlerCb handler_cb_;
  AdminStream& admin_stream_;
  Buffer::OwnedImpl response_;
};

} // namespace

Admin::RequestPtr Admin::makeStaticTextRequest(absl::string_view response, Http::Code code) {
  return std::make_unique<StaticTextRequest>(response, code);
}

Admin::RequestPtr Admin::makeStaticTextRequest(Buffer::Instance& response, Http::Code code) {
  return std::make_unique<StaticTextRequest>(response, code);
}

Http::Code AdminImpl::runCallback(Http::ResponseHeaderMap& response_headers,
                                  Buffer::Instance& response, AdminStream& admin_stream) {
  RequestPtr request = makeRequest(admin_stream);
  Http::Code code = request->start(response_headers);
  bool more_data;
  do {
    more_data = request->nextChunk(response);
  } while (more_data);
  Memory::Utils::tryShrinkHeap();
  return code;
}

Admin::RequestPtr AdminImpl::makeRequest(AdminStream& admin_stream) const {
  absl::string_view path_and_query = admin_stream.getRequestHeaders().getPathValue();
  std::string::size_type query_index = path_and_query.find('?');
  if (query_index == std::string::npos) {
    query_index = path_and_query.size();
  }

  for (const UrlHandler& handler : handlers_) {
    if (path_and_query.compare(0, query_index, handler.prefix_) == 0) {
      if (handler.mutates_server_state_) {
        const absl::string_view method = admin_stream.getRequestHeaders().getMethodValue();
        if (method != Http::Headers::get().MethodValues.Post) {
          ENVOY_LOG(error, "admin path \"{}\" mutates state, method={} rather than POST",
                    handler.prefix_, method);
          return Admin::makeStaticTextRequest(
              fmt::format("Method {} not allowed, POST required.", method),
              Http::Code::MethodNotAllowed);
        }
      }

      ASSERT(admin_stream.getRequestHeaders().getPathValue() == path_and_query);
      return handler.handler_(admin_stream);
    }
  }

  // Extra space is emitted below to have "invalid path." be a separate sentence in the
  // 404 output from "admin commands are:" in handlerHelp.
  Buffer::OwnedImpl error_response;
  error_response.add("invalid path. ");
  getHelp(error_response);
  return Admin::makeStaticTextRequest(error_response, Http::Code::NotFound);
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

Http::Code AdminImpl::handlerHelp(Http::ResponseHeaderMap&, Buffer::Instance& response,
                                  AdminStream&) {
  getHelp(response);
  return Http::Code::OK;
}

void AdminImpl::getHelp(Buffer::Instance& response) const {
  response.add("admin commands are:\n");

  // Prefix order is used during searching, but for printing do them in alpha order.
  for (const UrlHandler* handler : sortedHandlers()) {
    const absl::string_view method = handler->mutates_server_state_ ? " (POST)" : "";
    response.add(fmt::format("  {}{}: {}\n", handler->prefix_, method, handler->help_text_));
    for (const ParamDescriptor& param : handler->params_) {
      response.add(fmt::format("      {}: {}", param.id_, param.help_));
      if (param.type_ == ParamDescriptor::Type::Enum) {
        response.addFragments({"; One of (", absl::StrJoin(param.enum_choices_, ", "), ")"});
      }
      response.add("\n");
    }
  }
}

const Network::Address::Instance& AdminImpl::localAddress() {
  return *server_.localInfo().address();
}

AdminImpl::UrlHandler AdminImpl::makeHandler(const std::string& prefix,
                                             const std::string& help_text, HandlerCb callback,
                                             bool removable, bool mutates_state,
                                             const ParamDescriptorVec& params) {
  return UrlHandler{prefix,    help_text,     RequestGasket::makeGen(callback),
                    removable, mutates_state, params};
}

bool AdminImpl::addStreamingHandler(const std::string& prefix, const std::string& help_text,
                                    GenRequestFn callback, bool removable, bool mutates_state,
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

bool AdminImpl::addHandler(const std::string& prefix, const std::string& help_text,
                           HandlerCb callback, bool removable, bool mutates_state,
                           const ParamDescriptorVec& params) {
  return addStreamingHandler(prefix, help_text, RequestGasket::makeGen(callback), removable,
                             mutates_state, params);
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
  AdminFilter filter(createRequestFunction());

  auto request_headers = Http::RequestHeaderMapImpl::create();
  request_headers->setMethod(method);
  request_headers->setPath(path_and_query);
  filter.decodeHeaders(*request_headers, false);
  Buffer::OwnedImpl response;

  Http::Code code = runCallback(response_headers, response, filter);
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
    handler->addListener(absl::nullopt, *listener_, server_.runtime(),
                         server_.api().randomGenerator());
  }
}

#ifdef ENVOY_ENABLE_UHV
::Envoy::Http::HeaderValidatorStats&
AdminImpl::getHeaderValidatorStats([[maybe_unused]] Http::Protocol protocol) {
  switch (protocol) {
  case Http::Protocol::Http10:
  case Http::Protocol::Http11:
    return Http::Http1::CodecStats::atomicGet(http1_codec_stats_, *server_.stats().rootScope());
  case Http::Protocol::Http3:
    IS_ENVOY_BUG("HTTP/3 is not supported for admin UI");
    // Return H/2 stats object, since we do not have H/3 stats.
    ABSL_FALLTHROUGH_INTENDED;
  case Http::Protocol::Http2:
    return Http::Http2::CodecStats::atomicGet(http2_codec_stats_, *server_.stats().rootScope());
  }
  PANIC_DUE_TO_CORRUPT_ENUM;
}
#endif

} // namespace Server
} // namespace Envoy
