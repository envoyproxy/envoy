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

namespace {
// Implements a chunked request for Prometheus stats. The stats are snapshotted and grouped by
// metric family in start(), and nextChunk() renders whole families up to the chunk size.
class PrometheusRequest : public Admin::Request {
public:
  PrometheusRequest(Server::Instance& server, const StatsParams& params, AdminStream& admin_stream)
      : server_(server), params_(params), admin_stream_(admin_stream) {}

  Http::Code start(Http::ResponseHeaderMap& response_headers) override {
    const Http::RequestHeaderMap& request_headers = admin_stream_.getRequestHeaders();
    const absl::Status status = PrometheusStatsFormatter::validateParams(params_, request_headers);
    if (!status.ok()) {
      response_.add(status.message());
      return Http::Code::BadRequest;
    }
    if (server_.statsConfig().flushOnAdmin()) {
      server_.flushStats();
    }

    // The snapshots keep the stats alive until the request is done, even if their scopes are
    // deleted between chunks.
    Stats::Store& stats = server_.stats();
    counters_ = stats.counters();
    gauges_ = stats.gauges();
    histograms_ = stats.histograms();
    if (params_.prometheus_text_readouts_) {
      text_readouts_ = stats.textReadouts();
    }
    output_format_ =
        PrometheusStatsFormatter::makeOutputFormat(params_, request_headers, response_headers);
    renderer_ = std::make_unique<PrometheusStatsFormatter::ChunkedRenderer>(
        counters_, gauges_, histograms_, text_readouts_, server_.clusterManager(), params_,
        server_.api().customStatNamespaces(), *output_format_);
    return Http::Code::OK;
  }

  bool nextChunk(Buffer::Instance& response) override {
    if (renderer_ == nullptr) {
      response.move(response_);
      return false;
    }
    return renderer_->nextChunk(response, StatsRequest::DefaultChunkSize);
  }

private:
  Server::Instance& server_;
  const StatsParams params_;
  AdminStream& admin_stream_;
  Buffer::OwnedImpl response_;
  std::vector<Stats::CounterSharedPtr> counters_;
  std::vector<Stats::GaugeSharedPtr> gauges_;
  std::vector<Stats::ParentHistogramSharedPtr> histograms_;
  std::vector<Stats::TextReadoutSharedPtr> text_readouts_;
  std::unique_ptr<PrometheusStatsFormatter::OutputFormat> output_format_;
  std::unique_ptr<PrometheusStatsFormatter::ChunkedRenderer> renderer_;
};
} // namespace

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
    // Prometheus needs all stats with the same tag-extracted name in one group, and those come
    // from different scopes, so it cannot stream scope by scope like StatsRequest.
    return std::make_unique<PrometheusRequest>(server_, params, admin_stream);
  }

  if (params.histogram_buckets_mode_ == Utility::HistogramBucketsMode::PrometheusNative) {
    return Admin::makeStaticTextRequest(
        "Invalid histogram_buckets type for non prometheus stats type", Http::Code::BadRequest);
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

Admin::RequestPtr StatsHandler::makePrometheusRequest(AdminStream& admin_stream) {
  StatsParams params;
  Buffer::OwnedImpl response;
  const Http::Code code = params.parse(admin_stream.getRequestHeaders().getPathValue(), response);
  if (code != Http::Code::OK) {
    return Admin::makeStaticTextRequest(response, code);
  }
  return std::make_unique<PrometheusRequest>(server_, params, admin_stream);
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
                                           {"cumulative", "disjoint", "detailed", "summary"}};
  Admin::ParamDescriptor format{Admin::ParamDescriptor::Type::Enum,
                                "format",
                                "Format to use",
                                {"html", "active-html", "text", "json"}};
  Admin::ParamDescriptor filter{Admin::ParamDescriptor::Type::String, "filter",
                                "Regular expression (Google re2) for filtering stats"};
  Admin::ParamDescriptor invert_filter{Admin::ParamDescriptor::Type::Boolean, "invert_filter",
                                       "Invert the filter regex"};
  Admin::ParamDescriptor type{Admin::ParamDescriptor::Type::Enum,
                              "type",
                              "Stat types to include.",
                              {StatLabels::All, StatLabels::Counters, StatLabels::Histograms,
                               StatLabels::Gauges, StatLabels::TextReadouts}};

  Admin::ParamDescriptorVec params{usedonly, filter, invert_filter};
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
