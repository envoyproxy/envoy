#pragma once

#include <cstdint>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"

#include "source/common/http/utility.h"

namespace Envoy {
namespace Server {

enum class CpuInfoFormat {
  Json,
  Text,
};

struct CpuInfoParams {
  /**
   * Parses the URL's query parameters, populating this.
   *
   * Supported params:
   * - sampling_interval_ms: integer (default 1000)
   * - format: json|text (default text)
   */
  Http::Code parse(absl::string_view url, Buffer::Instance& response);

  static absl::string_view formatToString(CpuInfoFormat format);

  CpuInfoFormat format_{CpuInfoFormat::Text};
  uint64_t sampling_interval_ms_{1000};

  Http::Utility::QueryParamsMulti query_;
};

} // namespace Server
} // namespace Envoy


