#include "source/server/admin/stats_handler.h"

#include <functional>
#include <vector>

#include "envoy/admin/v3/mutex_stats.pb.h"
#include "envoy/server/admin.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/server/admin/prometheus_stats.h"
#include "source/server/admin/stats_request.h"

#include "absl/strings/numbers.h"

namespace Envoy {
namespace Server {

const uint64_t RecentLookupsCapacity = 100;

StatsHandler::StatsHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code StatsHandler::handlerResetCounters(Http::ResponseHeaderMap&, Buffer::Instance& response,
                                              AdminStream&) {
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    counter->reset();
  }
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookups(Http::ResponseHeaderMap&,
                                                   Buffer::Instance& response, AdminStream&) {
  Stats::SymbolTable& symbol_table = server_.stats().symbolTable();
  std::string table;
  const uint64_t total =
      symbol_table.getRecentLookups([&table](absl::string_view name, uint64_t count) {
        table += fmt::format("{:8d} {}\n", count, name);
      });
  if (table.empty() && symbol_table.recentLookupCapacity() == 0) {
    table = "Lookup tracking is not enabled. Use /stats/recentlookups/enable to enable.\n";
  } else {
    response.add("   Count Lookup\n");
  }
  response.add(absl::StrCat(table, "\ntotal: ", total, "\n"));
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsClear(Http::ResponseHeaderMap&,
                                                        Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsDisable(Http::ResponseHeaderMap&,
                                                          Buffer::Instance& response,
                                                          AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(0);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsEnable(Http::ResponseHeaderMap&,
                                                         Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(RecentLookupsCapacity);
  response.add("OK\n");
  return Http::Code::OK;
}

Admin::RequestPtr StatsHandler::makeRequest(AdminStream& admin_stream) {
  StatsParams params;
  Buffer::OwnedImpl response;
  Http::Code code = params.parse(admin_stream.getRequestHeaders().getPathValue(), response);
  if (code != Http::Code::OK) {
    return Admin::makeStaticTextRequest(response, code);
  }

  if (params.format_ == StatsFormat::Prometheus) {
    // TODO(#16139): modify streaming algorithm to cover Prometheus.
    //
    // This may be easiest to accomplish by populating the set
    // with tagExtractedName(), and allowing for vectors of
    // stats as multiples will have the same tag-extracted names.
    // Ideally we'd find a way to do this without slowing down
    // the non-Prometheus implementations.
    Buffer::OwnedImpl response;
    prometheusFlushAndRender(params, response);
    return Admin::makeStaticTextRequest(response, code);
  }

  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }

  bool active_mode;
#ifdef ENVOY_ADMIN_HTML
  active_mode = params.format_ == StatsFormat::ActiveHtml;
#else
  active_mode = false;
#endif
  return makeRequest(
      server_.stats(), params, server_.clusterManager(),
      [this, active_mode]() -> Admin::UrlHandler { return statsHandler(active_mode); });
}

Admin::RequestPtr StatsHandler::makeRequest(Stats::Store& stats, const StatsParams& params,
                                            const Upstream::ClusterManager& cluster_manager,
                                            StatsRequest::UrlHandlerFn url_handler_fn) {
  return std::make_unique<StatsRequest>(stats, params, cluster_manager, url_handler_fn);
}

Http::Code StatsHandler::handlerPrometheusStats(Http::ResponseHeaderMap&,
                                                Buffer::Instance& response,
                                                AdminStream& admin_stream) {
  return prometheusStats(admin_stream.getRequestHeaders().getPathValue(), response);
}

Http::Code StatsHandler::prometheusStats(absl::string_view path_and_query,
                                         Buffer::Instance& response) {
  StatsParams params;
  Http::Code code = params.parse(path_and_query, response);
  if (code != Http::Code::OK) {
    return code;
  }

  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }

  prometheusFlushAndRender(params, response);
  return Http::Code::OK;
}

void StatsHandler::prometheusFlushAndRender(const StatsParams& params, Buffer::Instance& response) {
  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }
  prometheusRender(server_.stats(), server_.api().customStatNamespaces(), server_.clusterManager(),
                   params, response);
}

void StatsHandler::prometheusRender(Stats::Store& stats,
                                    const Stats::CustomStatNamespaces& custom_namespaces,
                                    const Upstream::ClusterManager& cluster_manager,
                                    const StatsParams& params, Buffer::Instance& response) {
  const std::vector<Stats::TextReadoutSharedPtr>& text_readouts_vec =
      params.prometheus_text_readouts_ ? stats.textReadouts()
                                       : std::vector<Stats::TextReadoutSharedPtr>();
  PrometheusStatsFormatter::statsAsPrometheus(stats.counters(), stats.gauges(), stats.histograms(),
                                              text_readouts_vec, cluster_manager, response, params,
                                              custom_namespaces);
}

Http::Code StatsHandler::handlerContention(Http::ResponseHeaderMap& response_headers,
                                           Buffer::Instance& response, AdminStream&) {

  if (server_.options().mutexTracingEnabled() && server_.mutexTracer() != nullptr) {
    response_headers.setReferenceContentType(Http::Headers::get().ContentTypeValues.Json);

    envoy::admin::v3::MutexStats mutex_stats;
    mutex_stats.set_num_contentions(server_.mutexTracer()->numContentions());
    mutex_stats.set_current_wait_cycles(server_.mutexTracer()->currentWaitCycles());
    mutex_stats.set_lifetime_wait_cycles(server_.mutexTracer()->lifetimeWaitCycles());
    response.add(MessageUtil::getJsonStringFromMessageOrError(mutex_stats, true, true));
  } else {
    response.add("Mutex contention tracing is not enabled. To enable, run Envoy with flag "
                 "--enable-mutex-tracing.");
  }
  return Http::Code::OK;
}

Admin::UrlHandler StatsHandler::statsHandler(bool active_mode) {
  Admin::ParamDescriptor usedonly{
      Admin::ParamDescriptor::Type::Boolean, "usedonly",
      "Only include stats that have been written by system since restart"};
  Admin::ParamDescriptor histogram_buckets{Admin::ParamDescriptor::Type::Enum,
                                           "histogram_buckets",
                                           "Histogram bucket display mode",
                                           {"cumulative", "disjoint", "detailed", "none"}};
  Admin::ParamDescriptor format{Admin::ParamDescriptor::Type::Enum,
                                "format",
                                "Format to use",
                                {"html", "active-html", "text", "json"}};
  Admin::ParamDescriptor filter{Admin::ParamDescriptor::Type::String, "filter",
                                "Regular expression (Google re2) for filtering stats"};
  Admin::ParamDescriptor type{Admin::ParamDescriptor::Type::Enum,
                              "type",
                              "Stat types to include.",
                              {StatLabels::All, StatLabels::Counters, StatLabels::Histograms,
                               StatLabels::Gauges, StatLabels::TextReadouts}};

  Admin::ParamDescriptorVec params{usedonly, filter};
  if (!active_mode) {
    params.push_back(format);
  }
  params.push_back(type);
  if (!active_mode) {
    params.push_back(histogram_buckets);
  }

  return {
      "/stats",
      "print server stats",
      [this](AdminStream& admin_stream) -> Admin::RequestPtr { return makeRequest(admin_stream); },
      false,
      false,
      params};
}

} // namespace Server
} // namespace Envoy
