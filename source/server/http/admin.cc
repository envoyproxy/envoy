#include "server/http/admin.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_set>

#include "envoy/filesystem/filesystem.h"
#include "envoy/server/hot_restart.h"
#include "envoy/server/instance.h"
#include "envoy/server/options.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/upstream.h"

#include "common/access_log/access_log_formatter.h"
#include "common/access_log/access_log_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/enum_to_int.h"
#include "common/common/utility.h"
#include "common/common/version.h"
#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/headers.h"
#include "common/http/http1/codec_impl.h"
#include "common/json/json_loader.h"
#include "common/network/listen_socket_impl.h"
#include "common/profiler/profiler.h"
#include "common/router/config_impl.h"
#include "common/upstream/host_utility.h"

#include "fmt/format.h"

// TODO(mattklein123): Switch to JSON interface methods and remove rapidjson dependency.
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/prettywriter.h"
#include "rapidjson/reader.h"
#include "rapidjson/schema.h"
#include "rapidjson/stream.h"
#include "rapidjson/stringbuffer.h"
#include "spdlog/spdlog.h"

using namespace rapidjson;

namespace Envoy {
namespace Server {

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
    ENVOY_LOG(debug, "change all log levels: level='{}'", level);
    for (const Logger::Logger& logger : Logger::Registry::loggers()) {
      logger.setLevel(static_cast<spdlog::level::level_enum>(level_to_use));
    }
  } else {
    ENVOY_LOG(debug, "change log level: name='{}' level='{}'", name, level);
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

void AdminImpl::addOutlierInfo(const std::string& cluster_name,
                               const Upstream::Outlier::Detector* outlier_detector,
                               Buffer::Instance& response) {
  if (outlier_detector) {
    response.add(fmt::format("{}::outlier::success_rate_average::{}\n", cluster_name,
                             outlier_detector->successRateAverage()));
    response.add(fmt::format("{}::outlier::success_rate_ejection_threshold::{}\n", cluster_name,
                             outlier_detector->successRateEjectionThreshold()));
  }
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
    addOutlierInfo(cluster.second.get().info()->name(), cluster.second.get().outlierDetector(),
                   response);

    addCircuitSettings(
        cluster.second.get().info()->name(), "default",
        cluster.second.get().info()->resourceManager(Upstream::ResourcePriority::Default),
        response);
    addCircuitSettings(
        cluster.second.get().info()->name(), "high",
        cluster.second.get().info()->resourceManager(Upstream::ResourcePriority::High), response);

    response.add(fmt::format("{}::added_via_api::{}\n", cluster.second.get().info()->name(),
                             cluster.second.get().info()->addedViaApi()));

    for (auto& host : cluster.second.get().hosts()) {
      std::map<std::string, uint64_t> all_stats;
      for (const Stats::CounterSharedPtr& counter : host->counters()) {
        all_stats[counter->name()] = counter->value();
      }

      for (const Stats::GaugeSharedPtr& gauge : host->gauges()) {
        all_stats[gauge->name()] = gauge->value();
      }

      for (auto stat : all_stats) {
        response.add(fmt::format("{}::{}::{}::{}\n", cluster.second.get().info()->name(),
                                 host->address()->asString(), stat.first, stat.second));
      }

      response.add(fmt::format("{}::{}::health_flags::{}\n", cluster.second.get().info()->name(),
                               host->address()->asString(),
                               Upstream::HostUtility::healthFlagsToString(*host)));
      response.add(fmt::format("{}::{}::weight::{}\n", cluster.second.get().info()->name(),
                               host->address()->asString(), host->weight()));
      response.add(fmt::format("{}::{}::region::{}\n", cluster.second.get().info()->name(),
                               host->address()->asString(), host->locality().region()));
      response.add(fmt::format("{}::{}::zone::{}\n", cluster.second.get().info()->name(),
                               host->address()->asString(), host->locality().zone()));
      response.add(fmt::format("{}::{}::sub_zone::{}\n", cluster.second.get().info()->name(),
                               host->address()->asString(), host->locality().sub_zone()));
      response.add(fmt::format("{}::{}::canary::{}\n", cluster.second.get().info()->name(),
                               host->address()->asString(), host->canary()));
      response.add(fmt::format("{}::{}::success_rate::{}\n", cluster.second.get().info()->name(),
                               host->address()->asString(), host->outlierDetector().successRate()));
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
    if (!Profiler::Cpu::startProfiler(profile_path_)) {
      response.add("failure to start the profiler");
      return Http::Code::InternalServerError;
    }

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
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    counter->reset();
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

Http::Code AdminImpl::handlerStats(const std::string& url, Buffer::Instance& response) {
  // We currently don't support timers locally (only via statsd) so just group all the counters
  // and gauges together, alpha sort them, and spit them out.
  Http::Code rc = Http::Code::OK;
  const Http::Utility::QueryParams params = Http::Utility::parseQueryString(url);
  std::map<std::string, uint64_t> all_stats;
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    all_stats.emplace(counter->name(), counter->value());
  }

  for (const Stats::GaugeSharedPtr& gauge : server_.stats().gauges()) {
    all_stats.emplace(gauge->name(), gauge->value());
  }

  if (params.size() == 0) {
    // No Arguments so use the standard.
    for (auto stat : all_stats) {
      response.add(fmt::format("{}: {}\n", stat.first, stat.second));
    }
  } else {
    const std::string format_key = params.begin()->first;
    const std::string format_value = params.begin()->second;
    if (format_key == "format" && format_value == "json") {
      response.add(AdminImpl::statsAsJson(all_stats));
    } else if (format_key == "format" && format_value == "prometheus") {
      AdminImpl::statsAsPrometheus(server_.stats().counters(), server_.stats().gauges(), response);
    } else {
      response.add("usage: /stats?format=json \n");
      response.add("\n");
      rc = Http::Code::NotFound;
    }
  }
  return rc;
}

std::string AdminImpl::formatTagsForPrometheus(const std::vector<Stats::Tag>& tags) {
  std::vector<std::string> buf;
  for (const Stats::Tag& tag : tags) {
    buf.push_back(fmt::format("{}={}", tag.name_, tag.value_));
  }
  return StringUtil::join(buf, ",");
}

void AdminImpl::statsAsPrometheus(const std::list<Stats::CounterSharedPtr>& counters,
                                  const std::list<Stats::GaugeSharedPtr>& gauges,
                                  Buffer::Instance& response) {
  for (const auto& counter : counters) {
    const std::string tags = AdminImpl::formatTagsForPrometheus(counter->tags());
    response.add(fmt::format("# TYPE {0} counter\n", counter->tagExtractedName()));
    response.add(
        fmt::format("{0}{{{1}}} {2}\n", counter->tagExtractedName(), tags, counter->value()));
  }

  for (const auto& gauge : gauges) {
    const std::string tags = AdminImpl::formatTagsForPrometheus(gauge->tags());
    response.add(fmt::format("# TYPE {0} gauge\n", gauge->tagExtractedName()));
    response.add(fmt::format("{0}{{{1}}} {2}\n", gauge->tagExtractedName(), tags, gauge->value()));
  }
}

std::string AdminImpl::statsAsJson(const std::map<std::string, uint64_t>& all_stats) {
  rapidjson::Document document;
  document.SetObject();
  rapidjson::Value stats_array(rapidjson::kArrayType);
  rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
  for (auto stat : all_stats) {
    Value stat_obj;
    stat_obj.SetObject();
    Value stat_name;
    stat_name.SetString(stat.first.c_str(), allocator);
    stat_obj.AddMember("name", stat_name, allocator);
    Value stat_value;
    stat_value.SetInt(stat.second);
    stat_obj.AddMember("value", stat_value, allocator);
    stats_array.PushBack(stat_obj, allocator);
  }
  document.AddMember("stats", stats_array, allocator);
  rapidjson::StringBuffer strbuf;
  rapidjson::PrettyWriter<StringBuffer> writer(strbuf);
  document.Accept(writer);
  return strbuf.GetString();
}

Http::Code AdminImpl::handlerQuitQuitQuit(const std::string&, Buffer::Instance& response) {
  server_.shutdown();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerListenerInfo(const std::string&, Buffer::Instance& response) {
  std::list<std::string> listeners;
  for (auto listener : server_.listenerManager().listeners()) {
    listeners.push_back(listener.get().socket().localAddress()->asString());
  }
  response.add(Json::Factory::listAsJsonString(listeners));
  return Http::Code::OK;
}

Http::Code AdminImpl::handlerCerts(const std::string&, Buffer::Instance& response) {
  // This set is used to track distinct certificates. We may have multiple listeners, upstreams, etc
  // using the same cert.
  std::unordered_set<std::string> context_info_set;
  std::string context_format = "{{\n\t\"ca_cert\": \"{}\",\n\t\"cert_chain\": \"{}\"\n}}\n";
  server_.sslContextManager().iterateContexts([&](const Ssl::Context& context) -> void {
    context_info_set.insert(fmt::format(context_format, context.getCaCertInformation(),
                                        context.getCertChainInformation()));
  });

  std::string cert_result_string;
  for (const std::string& context_info : context_info_set) {
    cert_result_string += context_info;
  }
  response.add(cert_result_string);
  return Http::Code::OK;
}

void AdminFilter::onComplete() {
  std::string path = request_headers_->Path()->value().c_str();
  ENVOY_STREAM_LOG(debug, "request complete: path: {}", *callbacks_, path);

  Buffer::OwnedImpl response;
  Http::Code code = parent_.runCallback(path, response);

  Http::HeaderMapPtr headers{
      new Http::HeaderMapImpl{{Http::Headers::get().Status, std::to_string(enumToInt(code))}}};
  callbacks_->encodeHeaders(std::move(headers), response.length() == 0);

  if (response.length() > 0) {
    callbacks_->encodeData(response, true);
  }
}

AdminImpl::NullRouteConfigProvider::NullRouteConfigProvider()
    : config_(new Router::NullConfigImpl()) {}

AdminImpl::AdminImpl(const std::string& access_log_path, const std::string& profile_path,
                     const std::string& address_out_path,
                     Network::Address::InstanceConstSharedPtr address, Server::Instance& server)
    : server_(server), profile_path_(profile_path),
      socket_(new Network::TcpListenSocket(address, true)),
      stats_(Http::ConnectionManagerImpl::generateStats("http.admin.", server_.stats())),
      tracing_stats_(Http::ConnectionManagerImpl::generateTracingStats("http.admin.tracing.",
                                                                       server_.stats())),
      handlers_{
          {"/certs", "print certs on machine", MAKE_ADMIN_HANDLER(handlerCerts), false},
          {"/clusters", "upstream cluster status", MAKE_ADMIN_HANDLER(handlerClusters), false},
          {"/cpuprofiler", "enable/disable the CPU profiler",
           MAKE_ADMIN_HANDLER(handlerCpuProfiler), false},
          {"/healthcheck/fail", "cause the server to fail health checks",
           MAKE_ADMIN_HANDLER(handlerHealthcheckFail), false},
          {"/healthcheck/ok", "cause the server to pass health checks",
           MAKE_ADMIN_HANDLER(handlerHealthcheckOk), false},
          {"/hot_restart_version", "print the hot restart compatability version",
           MAKE_ADMIN_HANDLER(handlerHotRestartVersion), false},
          {"/logging", "query/change logging levels", MAKE_ADMIN_HANDLER(handlerLogging), false},
          {"/quitquitquit", "exit the server", MAKE_ADMIN_HANDLER(handlerQuitQuitQuit), false},
          {"/reset_counters", "reset all counters to zero",
           MAKE_ADMIN_HANDLER(handlerResetCounters), false},
          {"/server_info", "print server version/status information",
           MAKE_ADMIN_HANDLER(handlerServerInfo), false},
          {"/stats", "print server stats", MAKE_ADMIN_HANDLER(handlerStats), false},
          {"/listeners", "print listener addresses", MAKE_ADMIN_HANDLER(handlerListenerInfo),
           false}},
      listener_stats_(
          Http::ConnectionManagerImpl::generateListenerStats("http.admin.", server_.stats())) {

  if (!address_out_path.empty()) {
    std::ofstream address_out_file(address_out_path);
    if (!address_out_file) {
      ENVOY_LOG(critical, "cannot open admin address output file {} for writing.",
                address_out_path);
    } else {
      address_out_file << socket_->localAddress()->asString();
    }
  }

  access_logs_.emplace_back(new AccessLog::FileAccessLog(
      access_log_path, {}, AccessLog::AccessLogFormatUtils::defaultAccessLogFormatter(),
      server.accessLogManager()));
}

Http::ServerConnectionPtr AdminImpl::createCodec(Network::Connection& connection,
                                                 const Buffer::Instance&,
                                                 Http::ServerConnectionCallbacks& callbacks) {
  return Http::ServerConnectionPtr{
      new Http::Http1::ServerConnectionImpl(connection, callbacks, Http::Http1Settings())};
}

bool AdminImpl::createFilterChain(Network::Connection& connection) {
  connection.addReadFilter(Network::ReadFilterSharedPtr{new Http::ConnectionManagerImpl(
      *this, server_.drainManager(), server_.random(), server_.httpTracer(), server_.runtime(),
      server_.localInfo(), server_.clusterManager())});
  return true;
}

void AdminImpl::createFilterChain(Http::FilterChainFactoryCallbacks& callbacks) {
  callbacks.addStreamDecoderFilter(Http::StreamDecoderFilterSharedPtr{new AdminFilter(*this)});
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

const Network::Address::Instance& AdminImpl::localAddress() {
  return *server_.localInfo().address();
}

bool AdminImpl::addHandler(const std::string& prefix, const std::string& help_text,
                           HandlerCb callback, bool removable) {
  auto it = std::find_if(handlers_.cbegin(), handlers_.cend(),
                         [&prefix](const UrlHandler& entry) { return prefix == entry.prefix_; });
  if (it == handlers_.end()) {
    handlers_.push_back({prefix, help_text, callback, removable});
    return true;
  }
  return false;
}

bool AdminImpl::removeHandler(const std::string& prefix) {
  const uint size_before_removal = handlers_.size();
  handlers_.remove_if(
      [&prefix](const UrlHandler& entry) { return prefix == entry.prefix_ && entry.removable_; });
  if (handlers_.size() != size_before_removal) {
    return true;
  }
  return false;
}

} // namespace Server
} // namespace Envoy
