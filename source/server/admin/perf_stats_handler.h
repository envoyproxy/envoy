#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "server/admin/handler_ctx.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

/*
 * The admin handler for performance statistics (such as Header-map stats).
 * Requires compilation with --define=perf_annotation=enabled in order to return
 * non-empty results.
 */
class PerfStatsHandler : public HandlerContextBase {

public:
  PerfStatsHandler(Server::Instance& server);

  Http::Code handlerHeaderMapPerfStats(absl::string_view path_and_query,
                                       Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&);
  Http::Code handlerHeaderMapPerfClear(absl::string_view path_and_query,
                                       Http::ResponseHeaderMap& response_headers,
                                       Buffer::Instance& response, AdminStream&);
};

} // namespace Server
} // namespace Envoy
