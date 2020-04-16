#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class ListenersHandlerImpl {

public:
  static Http::Code handlerDrainListeners(absl::string_view path_and_query,
                                          Http::ResponseHeaderMap& response_headers,
                                          Buffer::Instance& response, AdminStream&,
                                          Server::Instance& server);

  static Http::Code handlerListenerInfo(absl::string_view path_and_query,
                                        Http::ResponseHeaderMap& response_headers,
                                        Buffer::Instance& response, AdminStream&,
                                        Server::Instance& server);

private:
  /**
   * Helper methods for the /listeners url handler.
   */
  static void writeListenersAsJson(Buffer::Instance& response, Server::Instance& server);
  static void writeListenersAsText(Buffer::Instance& response, Server::Instance& server);
};

} // namespace Server
} // namespace Envoy
