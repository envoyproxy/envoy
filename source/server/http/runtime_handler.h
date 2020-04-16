#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class RuntimeHandlerImpl {

public:
  static Http::Code handlerRuntime(absl::string_view path_and_query,
                                   Http::ResponseHeaderMap& response_headers,
                                   Buffer::Instance& response, AdminStream&,
                                   Server::Instance& server);
  static Http::Code handlerRuntimeModify(absl::string_view path_and_query,
                                         Http::ResponseHeaderMap& response_headers,
                                         Buffer::Instance& response, AdminStream&,
                                         Server::Instance& server);

private:
  static bool isFormUrlEncoded(const Http::HeaderEntry* content_type);
};

} // namespace Server
} // namespace Envoy
