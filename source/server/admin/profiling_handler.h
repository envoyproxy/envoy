#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Server {

class ProfilingHandler {

public:
  ProfilingHandler(const std::string& profile_path);

  Http::Code handlerCpuProfiler(Http::ResponseHeaderMap& response_headers,
                                Buffer::Instance& response, AdminStream&);

  Http::Code handlerHeapProfiler(Http::ResponseHeaderMap& response_headers,
                                 Buffer::Instance& response, AdminStream&);

private:
  const std::string profile_path_;
};

class TcmallocProfilingHandler {
public:
  TcmallocProfilingHandler() = default;

  Http::Code handlerHeapDump(Http::ResponseHeaderMap& response_headers, Buffer::Instance& response,
                             AdminStream&);
};

} // namespace Server
} // namespace Envoy
