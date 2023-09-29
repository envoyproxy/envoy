#pragma once

#include "absl/strings/string_view.h"

#include <string>
#include <vector>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class TraceState {
public:
  //<tenantID>-<clusterID>@dt=fw4;0;0;0;0;<isIgnored>;0;<rootPathRandom>;<extensionChecksum>;2h01;7h<spanId>
  static TraceState parse(std::string tracestate) {
    TraceState ret;
    std::vector<absl::string_view> tracestate_components =
        absl::StrSplit(tracestate, ';', absl::AllowEmpty());
    if (tracestate_components.size() > 7) {
      ret.is_ignored = tracestate_components[5];
      ret.sampling_exponent = tracestate_components[6];
      ret.path_info = tracestate_components[7];
      for (auto const& component : tracestate_components) {
        if (absl::StartsWith(component, "7h")) {
          ret.span_id = component;
        }
      }
    }
    return ret;
  }

  std::string tenant_id = "<tenant_id_t>";
  std::string cluster_id = "<cluster_id_t>";
  static constexpr absl::string_view at_dt_format = "@dt=fw4";
  static constexpr absl::string_view server_id = "0";
  static constexpr absl::string_view agent_d = "0";
  static constexpr absl::string_view tag_id = "0";
  static constexpr absl::string_view link_id = "0";
  std::string is_ignored = "0";
  std::string sampling_exponent = "0";
  std::string path_info = "0";
  std::string span_id = "";

  std::string toString() {
    // absl::StrCat
    std::string tracestate = absl::StrCat(
        absl::string_view(tenant_id), "-", absl::string_view(cluster_id), at_dt_format, ";", server_id, ";", agent_d, ";", tag_id, ";",
        link_id, ";", absl::string_view(is_ignored), ";", absl::string_view(sampling_exponent), ";",
        absl::string_view(path_info));

    if (!span_id.empty()) {
        std::string tmp(tracestate);
        tracestate = absl::StrCat(absl::string_view(tmp), ";", "7h", absl::string_view(span_id));
    }
    return tracestate;
  }
};
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy