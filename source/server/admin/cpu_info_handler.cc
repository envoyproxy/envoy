#include "source/server/admin/cpu_info_handler.h"

#include <functional>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

#include <fstream>
#include <sstream>

#include "source/common/common/fmt.h"

namespace Envoy {
namespace Server {

#if defined(__linux__)
// Read the system uptime in seconds from /proc/uptime.
bool readSystemUptime(double& uptime_seconds) {
  std::ifstream uptime_file("/proc/uptime");
  if (!uptime_file.is_open()) {
    return false;
  }
  uptime_file >> uptime_seconds;
  return uptime_file.good();
}

// Compute a thread's CPU utilization in percent, using a formula similar to Linux top:
//   total_time = (utime + stime) / ticks_per_second
//   seconds    = uptime_seconds - (starttime / ticks_per_second)
//   cpu%       = 100 * total_time / seconds / num_cpus
//
// The per-thread statistics are read from /proc/<pid>/task/<tid>/stat, using the field
// layout described in proc(5) and the same fields top.c relies on.
bool readThreadCpuPercent(pid_t pid, pid_t tid, double uptime_seconds, long ticks_per_second,
                          int num_cpus, double& cpu_percent) {
  if (ticks_per_second <= 0 || num_cpus <= 0) {
    return false;
  }

  const std::string stat_path = fmt::format("/proc/{}/task/{}/stat", pid, tid);
  std::ifstream stat_file(stat_path);
  if (!stat_file.is_open()) {
    return false;
  }

  std::string stat_line;
  std::getline(stat_file, stat_line);
  if (stat_line.empty()) {
    return false;
  }

  // The second field (comm) is wrapped in parentheses and may contain spaces. Find the
  // matching ')' and then parse the rest of the line as space-separated fields starting
  // from field #3 (state).
  const auto lparen = stat_line.find('(');
  const auto rparen = stat_line.rfind(')');
  if (lparen == std::string::npos || rparen == std::string::npos || rparen <= lparen) {
    return false;
  }

  // Skip ") " so that the first token in 'rest' corresponds to field #3 (state).
  const std::string rest = stat_line.substr(rparen + 2);
  std::istringstream iss(rest);

  // Relative indices from field #3 (state):
  //   utime     = field 14  -> index 11
  //   stime     = field 15  -> index 12
  //   starttime = field 22  -> index 19
  double utime_ticks = 0;
  double stime_ticks = 0;
  long long starttime_ticks = 0;

  std::string token;
  int index = 1;
  while (iss >> token) {
    if (index == 11) {
      utime_ticks = std::strtod(token.c_str(), nullptr);
    } else if (index == 12) {
      stime_ticks = std::strtod(token.c_str(), nullptr);
    } else if (index == 19) {
      starttime_ticks = std::strtoll(token.c_str(), nullptr, 10);
      break;
    }
    ++index;
  }

  if (starttime_ticks <= 0) {
    return false;
  }

  const double total_time_seconds =
      (utime_ticks + stime_ticks) / static_cast<double>(ticks_per_second);
  const double starttime_seconds =
      static_cast<double>(starttime_ticks) / static_cast<double>(ticks_per_second);
  const double seconds = uptime_seconds - starttime_seconds;

  if (seconds <= 0.0) {
    return false;
  }

  cpu_percent = (total_time_seconds / seconds) * (100.0 / static_cast<double>(num_cpus));
  return true;
}
#endif // defined(__linux__)

CpuInfoHandler::CpuInfoHandler(Server::Instance& server) : HandlerContextBase(server) {}

Http::Code CpuInfoHandler::handlerWorkersCpu(Http::ResponseHeaderMap&, Buffer::Instance& response,
                                             AdminStream&) {
#if defined(__linux__)
  const pid_t pid = getpid();
  const long ticks_per_second = sysconf(_SC_CLK_TCK);
  const int num_cpus = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));

  double uptime_seconds = 0;
  if (!readSystemUptime(uptime_seconds) || ticks_per_second <= 0 || num_cpus <= 0) {
    response.add("Worker CPU utilization is not available on this platform.\n");
    return Http::Code::OK;
  }

  response.add("Each worker thread CPU utilization (similar to Linux top):\n");
  for (const auto& worker : server_.workers()) {
    const pid_t tid = static_cast<pid_t>(worker->id());
    double cpu_percent = 0;
    if (readThreadCpuPercent(pid, tid, uptime_seconds, ticks_per_second, num_cpus, cpu_percent)) {
      response.add(fmt::format("Worker {} (tid {}): {:.2f}%\n", worker->id(), tid, cpu_percent));
    } else {
      response.add(fmt::format("Worker {} (tid {}): n/a\n", worker->id(), tid));
    }
  }
#else
  response.add("Worker CPU utilization is only supported on Linux.\n");
#endif

  return Http::Code::OK;
}

} // namespace Server
} // namespace Envoy

