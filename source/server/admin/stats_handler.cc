#include "source/server/admin/stats_handler.h"

#include <functional>
#include <vector>

#include "envoy/admin/v3/mutex_stats.pb.h"
#include "envoy/server/admin.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/server/admin/grouped_stats_request.h"
#include "source/server/admin/stats_params.h"
#include "source/server/admin/ungrouped_stats_request.h"

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

Admin::RequestPtr StatsHandler::makeRequest(AdminStream& admin_stream, StatsParams& params) {
  Buffer::OwnedImpl response;
  Http::Code code = params.parse(admin_stream.getRequestHeaders().getPathValue(), response);
  if (code != Http::Code::OK) {
    return Admin::makeStaticTextRequest(response, code);
  }

  if (server_.statsConfig().flushOnAdmin()) {
    server_.flushStats();
  }

  if (params.format_ == StatsFormat::Prometheus) {
    return makePrometheusRequest(
        server_.stats(), params, server_.api().customStatNamespaces(),
        [this]() -> Admin::UrlHandler { return prometheusStatsHandler(); });
  }
#ifdef ENVOY_ADMIN_HTML
  return makeRequest(server_.stats(), params,
                     [this, active_html = params.active_html_]() -> Admin::UrlHandler {
                       return statsHandler("/stats", active_html);
                     });
#else
  return makeRequest(server_.stats(), params,
                     [this]() -> Admin::UrlHandler { return statsHandler("/stats", false); });
#endif
}

Admin::RequestPtr StatsHandler::makeRequest(Stats::Store& stats, const StatsParams& params,
                                            UngroupedStatsRequest::UrlHandlerFn url_handler_fn) {
  return std::make_unique<UngroupedStatsRequest>(stats, params, url_handler_fn);
}

Admin::RequestPtr
StatsHandler::makePrometheusRequest(Stats::Store& stats, const StatsParams& params,
                                    Stats::CustomStatNamespaces& custom_namespaces,
                                    GroupedStatsRequest::UrlHandlerFn url_handler_fn) {
  return std::make_unique<GroupedStatsRequest>(stats, params, custom_namespaces, url_handler_fn);
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

Admin::UrlHandler StatsHandler::statsHandler(const std::string& prefix, bool active_html) {
  Admin::ParamDescriptor usedonly{
      Admin::ParamDescriptor::Type::Boolean, "usedonly",
      "Only include stats that have been written by system since restart"};
  Admin::ParamDescriptor filter{Admin::ParamDescriptor::Type::String, "filter",
                                "Regular expression (Google re2) for filtering stats"};
  Admin::ParamDescriptor format{
      Admin::ParamDescriptor::Type::Enum, "format", "Format to use", {"html", "text", "json"}};
  Admin::ParamDescriptor type{Admin::ParamDescriptor::Type::Enum,
                              "type",
                              "Stat types to include.",
                              {StatLabels::All, StatLabels::Counters, StatLabels::Histograms,
                               StatLabels::Gauges, StatLabels::TextReadouts}};
  Admin::ParamDescriptor histogram_buckets{Admin::ParamDescriptor::Type::Enum,
                                           "histogram_buckets",
                                           "Histogram bucket display mode",
                                           {"cumulative", "disjoint", "none", "detailed"}};

  Admin::ParamDescriptorVec params;
  if (active_html) {
    params.push_back(filter);
    params.push_back(type);
  } else {
    params.push_back(usedonly);
    params.push_back(filter);
    params.push_back(format);
    params.push_back(type);
    params.push_back(histogram_buckets);
  }

  return {prefix,
          "print server stats",
          [this, active_html](AdminStream& admin_stream) -> Admin::RequestPtr {
            StatsParams params;
            params.active_html_ = active_html;
            return makeRequest(admin_stream, params);
          },
          false,
          false,
          params};
}

Admin::UrlHandler StatsHandler::prometheusStatsHandler() {
  return {"/stats/prometheus",
          "print server stats in prometheus format",
          [this](AdminStream& admin_stream) -> Admin::RequestPtr {
            StatsParams params;
            params.format_ = StatsFormat::Prometheus;
            return makeRequest(admin_stream, params);
          },
          false,
          false,
          {{Admin::ParamDescriptor::Type::Boolean, "usedonly",
            "Only include stats that have been written by system since restart"},
           {Admin::ParamDescriptor::Type::Boolean, "text_readouts",
            "Render text_readouts as new gaugues with value 0 (increases Prometheus "
            "data size)"},
           {Admin::ParamDescriptor::Type::String, "filter",
            "Regular expression (Google re2) for filtering stats"}}};
}

} // namespace Server
} // namespace Envoy
