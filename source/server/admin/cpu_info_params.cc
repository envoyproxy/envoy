#include "source/server/admin/cpu_info_params.h"

namespace Envoy {
namespace Server {

Http::Code CpuInfoParams::parse(absl::string_view url, Buffer::Instance& response) {
  query_ = Http::Utility::QueryParamsMulti::parseAndDecodeQueryString(url);

  // format: json|text
  if (const absl::optional<std::string> format_value = query_.getFirstValue("format");
      format_value.has_value() && !format_value.value().empty()) {
    if (format_value.value() == "json") {
      format_ = CpuInfoFormat::Json;
    } else if (format_value.value() == "text") {
      format_ = CpuInfoFormat::Text;
    } else {
      response.add("usage: /cpu/workers?format=(json|text)\n\n");
      return Http::Code::BadRequest;
    }
  }

  // sampling_interval_ms: integer (default 1000)
  if (const absl::optional<std::string> sampling_value = query_.getFirstValue("sampling_interval_ms");
      sampling_value.has_value() && !sampling_value.value().empty()) {
    uint64_t parsed = 0;
    if (!absl::SimpleAtoi(sampling_value.value(), &parsed) || parsed == 0) {
      response.add("usage: /cpu/workers?sampling_interval_ms=<positive integer>\n\n");
      return Http::Code::BadRequest;
    }
    sampling_interval_ms_ = parsed;
  }

  return Http::Code::OK;
}

absl::string_view CpuInfoParams::formatToString(CpuInfoFormat format) {
  switch (format) {
  case CpuInfoFormat::Json:
    return "json";
  case CpuInfoFormat::Text:
    return "text";
  }
  return "json";
}

} // namespace Server
} // namespace Envoy


