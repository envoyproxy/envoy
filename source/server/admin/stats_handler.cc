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

Http::Code StatsHandler::handlerResetCounters(absl::string_view, Http::ResponseHeaderMap&,
                                              Buffer::Instance& response, AdminStream&) {
  for (const Stats::CounterSharedPtr& counter : server_.stats().counters()) {
    counter->reset();
  }
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookups(absl::string_view, Http::ResponseHeaderMap&,
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

Http::Code StatsHandler::handlerStatsRecentLookupsClear(absl::string_view, Http::ResponseHeaderMap&,
                                                        Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().clearRecentLookups();
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsDisable(absl::string_view,
                                                          Http::ResponseHeaderMap&,
                                                          Buffer::Instance& response,
                                                          AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(0);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code StatsHandler::handlerStatsRecentLookupsEnable(absl::string_view,
                                                         Http::ResponseHeaderMap&,
                                                         Buffer::Instance& response, AdminStream&) {
  server_.stats().symbolTable().setRecentLookupCapacity(RecentLookupsCapacity);
  response.add("OK\n");
  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
