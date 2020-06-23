#include "server/admin/profiling_handler.h"

#include "common/profiler/profiler.h"

#include "server/admin/utils.h"

namespace Envoy {
namespace Server {

ProfilingHandler::ProfilingHandler(const std::string& profile_path) : profile_path_(profile_path) {}

Http::Code ProfilingHandler::handlerCpuProfiler(absl::string_view url, Http::ResponseHeaderMap&,
                                                Buffer::Instance& response, AdminStream&) {
  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  if (query_params.size() != 1 || query_params.begin()->first != "enable" ||
      (query_params.begin()->second != "y" && query_params.begin()->second != "n")) {
    response.add("?enable=<y|n>\n");
    return Http::Code::BadRequest;
  }

  bool enable = query_params.begin()->second == "y";
  if (enable && !Profiler::Cpu::profilerEnabled()) {
    if (!Profiler::Cpu::startProfiler(profile_path_)) {
      response.add("failure to start the profiler");
      return Http::Code::InternalServerError;
    }

  } else if (!enable && Profiler::Cpu::profilerEnabled()) {
    Profiler::Cpu::stopProfiler();
  }

  response.add("OK\n");
  return Http::Code::OK;
}

Http::Code ProfilingHandler::handlerHeapProfiler(absl::string_view url, Http::ResponseHeaderMap&,
                                                 Buffer::Instance& response, AdminStream&) {
  if (!Profiler::Heap::profilerEnabled()) {
    response.add("The current build does not support heap profiler");
    return Http::Code::NotImplemented;
  }

  Http::Utility::QueryParams query_params = Http::Utility::parseQueryString(url);
  if (query_params.size() != 1 || query_params.begin()->first != "enable" ||
      (query_params.begin()->second != "y" && query_params.begin()->second != "n")) {
    response.add("?enable=<y|n>\n");
    return Http::Code::BadRequest;
  }

  Http::Code res = Http::Code::OK;
  bool enable = query_params.begin()->second == "y";
  if (enable) {
    if (Profiler::Heap::isProfilerStarted()) {
      response.add("Fail to start heap profiler: already started");
      res = Http::Code::BadRequest;
    } else if (!Profiler::Heap::startProfiler(profile_path_)) {
      // GCOVR_EXCL_START
      // TODO(silentdai) remove the GCOVR when startProfiler is better implemented
      response.add("Fail to start the heap profiler");
      res = Http::Code::InternalServerError;
      // GCOVR_EXCL_STOP
    } else {
      response.add("Starting heap profiler");
      res = Http::Code::OK;
    }
  } else {
    // !enable
    if (!Profiler::Heap::isProfilerStarted()) {
      response.add("Fail to stop heap profiler: not started");
      res = Http::Code::BadRequest;
    } else {
      Profiler::Heap::stopProfiler();
      response.add(
          fmt::format("Heap profiler stopped and data written to {}. See "
                      "http://goog-perftools.sourceforge.net/doc/heap_profiler.html for details.",
                      profile_path_));
      res = Http::Code::OK;
    }
  }
  return res;
}

} // namespace Server
} // namespace Envoy
