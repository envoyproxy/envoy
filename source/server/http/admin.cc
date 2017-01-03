#include "admin.h"

#include "envoy/filesystem/filesystem.h"
#include "envoy/server/hot_restart.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/common/version.h"
#include "common/http/access_log/access_log_formatter.h"
#include "common/http/access_log/access_log_impl.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/http1/codec_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/profiler/profiler.h"
#include "common/router/config_impl.h"
#include "common/upstream/host_utility.h"

namespace Server {

#define MAKE_HANDLER(X)                                                                            \
  [this](const std::string& url, Buffer::Instance& data) -> Http::Code { return X(url, data); }

AdminFilter::AdminFilter(AdminImpl& parent) : parent_(parent) {}

Http::FilterHeadersStatus AdminFilter::decodeHeaders(Http::HeaderMap& headers, bool end_stream) {
  request_headers_ = &headers;
  if (end_stream) {
    onComplete();
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus AdminFilter::decodeData(Buffer::Instance&, bool end_stream) {
  if (end_stream) {
    onComplete();
  }

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus AdminFilter::decodeTrailers(Http::HeaderMap&) {
  onComplete();
  return Http::FilterTrailersStatus::StopIteration;
}

bool AdminImpl::changeLogLevel(const Http::Utility::QueryParams& params) {
  if (params.size() != 1) {
    return false;
  }

  std::string name = params.begin()->first;
  std::string level = params.begin()->second;

  // First see if the level is valid.
  size_t level_to_use = std::numeric_limits<size_t>::max();
  for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_names); i++) {
    if (level == spdlog::level::level_names[i]) {
      level_to_use = i;
      break;
    }
  }

  if (level_to_use == std::numeric_limits<size_t>::max()) {
    return false;
  }

  // Now either change all levels or a single level.
  if (name == "level") {
    log_debug("change all log levels: level='{}'", level);
    for (const Logger::Logger& logger : Logger::Registry::loggers()) {
      logger.setLevel(static_cast<spdlog::level::level_enum>(level_to_use));
    }
  } else {
    log_debug("change log level: name='{}' level='{}'", name, level);
    const Logger::Logger* logger_to_change = nullptr;
    for (const Logger::Logger& logger : Logger::Registry::loggers()) {
      if (logger.name() == name) {
        logger_to_change = &logger;
        break;
      }
    }

    if (!logger_to_change) {
      return false;
    }

    logger_to_change->setLevel(static_cast<spdlog::level::level_enum>(level_to_use));
  }

  return true;
}

void AdminImpl::addCircuitSettings(const std::string& cluster_name, const std::string& priority_str,
                                   Upstream::ResourceManager& resource_manager,
                                   Buffer::Instance& response) {
  response.add(fmt::format("{}::{}_priority::max_connections::{}\n", cluster_name, priority_str,
                           resource_manager.connections().max()));
  response.add(fmt::format("{}::{}_priority::max_pending_requests::{}\n", cluster_name,
                           priority_str, resource_manager.pendingRequests().max()));
  response.add(fmt::format("{}::{}_priority::max_requests::{}\n", cluster_name, priority_str,
                           resource_manager.requests().max()));
  response.add(fmt::format("{}::{}_priority::max_retries::{}\n", cluster_name, priority_str,
                           resource_manager.retries().max()));
}

Http::Code AdminImpl::handlerClusters(const std::string&, Buffer::Instance& response) {
  for (auto& cluster : server_.clusterManager().clusters()) {
    addCircuitSettings(
        cluster.second.get().info()->name(), "default",
        cluster.second.get().info()->resourceManager(Upstream::ResourcePriority::Default),
        response);
    addCircuitSettings(
        cluster.second.get().info()->name(), "high",
        cluster.second.get().info()->resourceManager(Upstream::ResourcePriority::High), response);

    for (auto& host : cluster.second.get().hosts()) {
      std::map<std::string, uint64_t> all_stats;
      for (Stats::Counter& counter : host->counters()) {
        all_stats[counter.name()] = counter.value();
      }

      for (Stats::Gauge& gauge : host->gauges()) {
        all_stats[gauge.name()] = gauge.value();
      }

      for (auto stat : all_stats) {
        response.add(fmt::format("{}::{}::{}::{}\n", cluster.second.get().info()->name(),
                                 host->url(), stat.first, stat.second));
      }

      response.add(fmt::format("{}::{}::health_flags::{}\n", cluster.second.get().info()->name(),
                               host->url(), Upstream::HostUtility::healthFlagsToString(*host)));
      response.add(fmt::format("{}::{}::weight::{}\n", cluster.second.get().info()->name(),
                               host->url(), host->weight()));
      response.add(fmt::format("{}::{}::zone::{}\n", cluster.second.get().info()->name(),
                               host->url(), host->zone()));
      response.add(fmt::format("{}::{}::canary::{}\n", cluster.second.get().info()->name(),
                               host->url(), host->canary()));
    }
  }

  return Http::Code::OK;
}

Http::Code AdminImpl::handlerCpuProfiler(const std::string& url, Buffer::Instance& response) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  if (query_params.size() != 1 || query_params.begin()->first != "enable" ||
      (query_params.begin()->second != "y" && query_params.begin()->second != "n")) {
    response.add("?enable=<y|n>\n");
    return Http::Code::BadRequest;
  }

  bool enable = query_params.begin()->second == "y";
  if (enable && !Profiler::Cpu::profilerEnabled()) {
    Profiler::Cpu::startProfiler("/var/log/envoy/envoy.prof");
  } else if (!enable && Profiler::Cpu::profilerEnabled()) {
    Profiler::Cpu::stopProfiler();
  }

  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerHealthcheckFail(const std::string&, Buffer::Instance& response) {
  server_.failHealthcheck(true);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerHealthcheckOk(const std::string&, Buffer::Instance& response) {
  server_.failHealthcheck(false);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerHotRestartVersion(const std::string&, Buffer::Instance& response) {
  response.add(server_.hotRestart().version());
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerLogging(const std::string& url, Buffer::Instance& response) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);

  Http::Code rc = Http::Code::OK;
  if (!changeLogLevel(query_params)) {
    response.add("usage: /logging?<name>=<level> (change single level)\n");
    response.add("usage: /logging?level=<level> (change all levels)\n");
    response.add("levels: ");
    for (size_t i = 0; i < ARRAY_SIZE(spdlog::level::level_names); i++) {
      response.add(fmt::format("{} ", spdlog::level::level_names[i]));
    }

    response.add("\n");
    rc = Http::Code::NotFound;
  }

  response.add("active loggers:\n");
  for (const Logger::Logger& logger : Logger::Registry::loggers()) {
    response.add(fmt::format("  {}: {}\n", logger.name(), logger.levelString()));
  }

  response.add("\n");
  return rc;
}

Http::Code AdminImpl::handlerResetCounters(const std::string&, Buffer::Instance& response) {
  for (Stats::Counter& counter : server_.stats().counters()) {
    counter.reset();
  }

  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerServerInfo(const std::string&, Buffer::Instance& response) {
  time_t current_time = time(nullptr);
  response.add(fmt::format("envoy {} {} {} {} {}\n", VersionInfo::version(),
                           server_.healthCheckFailed() ? "draining" : "live",
                           current_time - server_.startTimeCurrentEpoch(),
                           current_time - server_.startTimeFirstEpoch(),
                           server_.options().restartEpoch()));
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerStats(const std::string&, Buffer::Instance& response) {
  // We currently don't support timers locally (only via statsd) so just group all the counters
  // and gauges together, alpha sort them, and spit them out.
  std::map<std::string, uint64_t> all_stats;
  for (Stats::Counter& counter : server_.stats().counters()) {
    all_stats.emplace(counter.name(), counter.value());
  }

  for (Stats::Gauge& gauge : server_.stats().gauges()) {
    all_stats.emplace(gauge.name(), gauge.value());
  }

  for (auto stat : all_stats) {
    response.add(fmt::format("{}: {}\n", stat.first, stat.second));
  }

  return Http::Code::OK;
}

Http::Code AdminImpl::handlerQuitQuitQuit(const std::string&, Buffer::Instance& response) {
  server_.shutdown();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerCerts(const std::string&, Buffer::Instance& response) {
  // This set is used to track distinct certificates. We may have multiple listeners, upstreams, etc
  // using the same cert.
  std::unordered_set<std::string> context_info_set;
  std::string context_format = "{{\n\t\"ca_cert\": \"{}\"\n\t\"cert_chain\": \"{}\"\n}}\n";
  for (Ssl::Context& context : server_.sslContextManager().getContexts()) {
    context_info_set.insert(fmt::format(context_format, context.getCaCertInformation(),
                                        context.getCertChainInformation()));
  }

  std::string cert_result_string;
  for (const std::string& context_info : context_info_set) {
    cert_result_string += context_info;
  }
  response.add(cert_result_string);
  return Http::Code::OK;
}

void AdminFilter::onComplete() {
  std::string path = request_headers_->Path()->value().c_str();
  stream_log_info("request complete: path: {}", *callbacks_, path);

  Buffer::OwnedImpl response;
  Http::Code code = parent_.runCallback(path, response);

  Http::HeaderMapPtr headers{
      new Http::HeaderMapImpl{{Http::Headers::get().Status, std::to_string(enumToInt(code))}}};
  callbacks_->encodeHeaders(std::move(headers), response.length() == 0);

  if (response.length() > 0) {
    callbacks_->encodeData(response, true);
  }
}

AdminImpl::AdminImpl(const std::string& access_log_path, uint32_t port, Server::Instance& server)
    : server_(server), socket_(new Network::TcpListenSocket(port)),
      stats_(Http::ConnectionManagerImpl::generateStats("http.admin.", server_.stats())),
      route_config_(new Router::NullConfigImpl()),
      handlers_{
          {"/certs", "print certs on machine", MAKE_HANDLER(handlerCerts)},
          {"/clusters", "upstream cluster status", MAKE_HANDLER(handlerClusters)},
          {"/cpuprofiler", "enable/disable the CPU profiler", MAKE_HANDLER(handlerCpuProfiler)},
          {"/healthcheck/fail", "cause the server to fail health checks",
           MAKE_HANDLER(handlerHealthcheckFail)},
          {"/healthcheck/ok", "cause the server to pass health checks",
           MAKE_HANDLER(handlerHealthcheckOk)},
          {"/hot_restart_version", "print the hot restart compatability version",
           MAKE_HANDLER(handlerHotRestartVersion)},
          {"/logging", "query/change logging levels", MAKE_HANDLER(handlerLogging)},
          {"/quitquitquit", "exit the server", MAKE_HANDLER(handlerQuitQuitQuit)},
          {"/reset_counters", "reset all counters to zero", MAKE_HANDLER(handlerResetCounters)},
          {"/server_info", "print server version/status information",
           MAKE_HANDLER(handlerServerInfo)},
          {"/stats", "print server stats", MAKE_HANDLER(handlerStats)}} {

  access_logs_.emplace_back(new Http::AccessLog::InstanceImpl(
      access_log_path, {}, Http::AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter(),
      server.accessLogManager()));
}

Http::ServerConnectionPtr AdminImpl::createCodec(Network::Connection& connection,
                                                 const Buffer::Instance&,
                                                 Http::ServerConnectionCallbacks& callbacks) {
  return Http::ServerConnectionPtr{new Http::Http1::ServerConnectionImpl(connection, callbacks)};
}

void AdminImpl::createFilterChain(Network::Connection& connection) {
  connection.addReadFilter(Network::ReadFilterPtr{new Http::ConnectionManagerImpl(
      *this, server_.drainManager(), server_.random(), server_.httpTracer(), server_.runtime())});
}

void AdminImpl::createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) {
  callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterPtr{new AdminFilter(*this)});
}

Http::Code AdminImpl::runCallback(const std::string& path, Buffer::Instance& response) {
  Http::Code code = Http::Code::OK;
  bool found_handler = false;
  for (const UrlHandler& handler : handlers_) {
    if (path.find(handler.prefix_) == 0) {
      code = handler.handler_(path, response);
      found_handler = true;
      break;
    }
  }

  if (!found_handler) {
    code = Http::Code::NotFound;
    response.add("envoy admin commands:\n");

    // Prefix order is used during searching, but for printing do them in alpha order.
    std::map<std::string, const UrlHandler*> sorted_handlers;
    for (const UrlHandler& handler : handlers_) {
      sorted_handlers[handler.prefix_] = &handler;
    }

    for (auto handler : sorted_handlers) {
      response.add(fmt::format("  {}: {}\n", handler.first, handler.second->help_text_));
    }
  }

  return code;
}

const std::string& AdminImpl::localAddress() { return server_.getLocalAddress(); }

} // Server
