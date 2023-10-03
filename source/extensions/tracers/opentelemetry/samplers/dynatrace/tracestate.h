#pragma once

#include "murmur.h"

#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"

#include <cstdint>
#include <string>
#include <sstream>
#include <vector>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

class TraceState {
public:
  static std::string hash_extension(const std::string& extension) {
    uint64_t hash = murmurHash264(extension.c_str(), extension.size());
    // hash &= 0xffff;
    uint16_t hash_16 = static_cast<uint16_t>(hash);
    std::stringstream stream;
    stream << std::hex << hash_16;
    std::string hash_as_hex(stream.str());
    return hash_as_hex;
  }

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
    std::string tracestate = absl::StrCat(
        absl::string_view(tenant_id), "-", absl::string_view(cluster_id), at_dt_format, ";",
        server_id, ";", agent_d, ";", tag_id, ";", link_id, ";", absl::string_view(is_ignored), ";",
        absl::string_view(sampling_exponent), ";", absl::string_view(path_info));

    //    Which is: "abcdabcd-77@dt=fw4;8;66666666;111;99;0;0;66;;8cef;2h01;7h293e72b548735604"
    if (!span_id.empty()) {
      std::string tmp(tracestate);
      // https://oaad.lab.dynatrace.org/agent/concepts/purepath/tagging/#forward-tags-fw
      std::string extension = ";7h" + span_id;
      std::string hash_as_hex = hash_extension(extension);
      tracestate = absl::StrCat(absl::string_view(tmp), ";", absl::string_view(hash_as_hex),
                                absl::string_view(extension));
    }
    return tracestate;
  }
};
} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy