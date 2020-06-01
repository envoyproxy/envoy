#include "server/admin/server_cmd_handler.h"

namespace Envoy {
namespace Server {

ServerCmdHandler::ServerCmdHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code ServerCmdHandler::handlerHealthcheckFail(absl::string_view, Http::ResponseHeaderMap&,
                                                    Buffer::Instance& response, AdminStream&) {
  server_.failHealthcheck(true);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code ServerCmdHandler::handlerHealthcheckOk(absl::string_view, Http::ResponseHeaderMap&,
                                                  Buffer::Instance& response, AdminStream&) {
  server_.failHealthcheck(false);
  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code ServerCmdHandler::handlerQuitQuitQuit(absl::string_view, Http::ResponseHeaderMap&,
                                                 Buffer::Instance& response, AdminStream&) {
  server_.shutdown();
  response.add("OK\n");
  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy
