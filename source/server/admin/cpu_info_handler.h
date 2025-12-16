#pragma once

#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"
#include "envoy/http/header_map.h"
#include "envoy/server/admin.h"
#include "envoy/server/instance.h"

#include "source/server/admin/cpu_info_params.h"
#include "source/server/admin/handler_ctx.h"

namespace Envoy {
namespace Server {

#if defined(__linux__)
#include <sys/types.h>

#include "absl/container/flat_hash_map.h"

// Minimal subset of /proc/<pid>/stat fields we currently care about.
// Field meanings are described in proc(5).
struct proc_t {
  std::string comm;                 // field 2 (inside parentheses)
  unsigned long long utime{0};      // field 14
  unsigned long long stime{0};      // field 15
  unsigned long long cutime{0};     // field 16
  unsigned long long cstime{0};     // field 17
  unsigned long long start_time{0}; // field 22 (jiffies since boot)
};

// Sample data for a single thread (worker or main).
struct ThreadSample {
  unsigned long long utime{0};
  unsigned long long stime{0};
};

// Collection of CPU samples for all worker threads + optional main thread.
struct EnvoyThreadCpuStatSamples {
  absl::flat_hash_map<uint32_t, ThreadSample> workers;
  bool has_main{false};
  ThreadSample main{};
};

#endif

class CpuInfoHandler : public HandlerContextBase {
public:
  CpuInfoHandler(Server::Instance& server);

  Http::Code handlerWorkersCpu(Http::ResponseHeaderMap& response_headers,
                               Buffer::Instance& response, AdminStream& admin_stream);

private:
  Http::Code returnError(absl::string_view msg, CpuInfoFormat format,
                         Http::ResponseHeaderMap& response_headers, Buffer::Instance& response);
#if defined(__linux__)
  long getHertz();
  double getBootTimeSeconds();
  bool stat2proc(const char* stat_line, proc_t& P);
  EnvoyThreadCpuStatSamples readEnvoyThreadSamples(pid_t pid, uint32_t concurrency);
  Http::Code measureDeltaCpuUtilization(uint64_t sampling_interval_ms, CpuInfoFormat format,
                                        Http::ResponseHeaderMap& response_headers,
                                        Buffer::Instance& response);
#endif
};

} // namespace Server
} // namespace Envoy


