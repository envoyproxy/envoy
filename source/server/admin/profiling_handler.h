#pragma once

#include "include/envoy/buffer/buffer.h"
#include "include/envoy/http/codes.h"
#include "include/envoy/http/header_map.h"
#include "include/envoy/server/admin.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class ProfilingHandler {

public:
  ProfilingHandler(const std::string& profile_path);

  Http::Code handlerCpuProfiler(absl::string_view path_and_query,
                                Http::ResponseHeaderMap& response_headers,
                                Buffer::Instance& response, AdminStream&);

  Http::Code handlerHeapProfiler(absl::string_view path_and_query,
                                 Http::ResponseHeaderMap& response_headers,
                                 Buffer::Instance& response, AdminStream&);

private:
  const std::string profile_path_;
};

} // namespace Server
} // namespace Envoy
