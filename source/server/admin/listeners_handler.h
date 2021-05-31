#pragma once

#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "server/admin/handler_ctx.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class ListenersHandler : public HandlerContextBase {

public:
  ListenersHandler(Server::Instance& server);

  Http::Code handlerDrainListeners(absl::string_view path_and_query,
                                   Http::ResponseHeaderMap& response_headers,
                                   Chunker& response, AdminStream&);

  Http::Code handlerListenerInfo(absl::string_view path_and_query,
                                 Http::ResponseHeaderMap& response_headers,
                                 Chunker& response, AdminStream&);

private:
  /**
   * Helper methods for the /listeners url handler.
   */
  void writeListenersAsJson(Chunker& response);
  void writeListenersAsText(Chunker& response);
};

} // namespace Server
} // namespace Envoy
