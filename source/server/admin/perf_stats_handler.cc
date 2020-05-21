#include "server/admin/perf_stats_handler.h"

#include "envoy/http/codes.h"

#include "common/common/perf_annotation.h"

namespace Envoy {
namespace Server {

PerfStatsHandler::PerfStatsHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code PerfStatsHandler::handlerHeaderMapPerfStats(absl::string_view, Http::ResponseHeaderMap&,
                                                       Buffer::Instance& response, AdminStream&) {
  std::string perf_stats = PERF_TO_STRING();
  if (perf_stats == "") {
    response.add("Header-Map perf stats is not enabled. Compile with "
                 "--define=perf_annotation=enabled to enable.\n");
  } else {
    response.add(absl::StrCat("Performance stats:\n", perf_stats));
  }
  return Http::Code::OK;
}

Http::Code PerfStatsHandler::handlerHeaderMapPerfClear(absl::string_view, Http::ResponseHeaderMap&,
                                                       Buffer::Instance& response, AdminStream&) {
  PERF_CLEAR();
  response.add("OK\n");
  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
