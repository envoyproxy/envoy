#pragma once

#include <regex>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"

#include "source/server/admin/utils.h"

namespace Envoy {
namespace Server {

namespace Labels {
constexpr absl::string_view All = "All";
constexpr absl::string_view Counters = "Counters";
constexpr absl::string_view Gauges = "Gauges";
constexpr absl::string_view Histograms = "Histograms";
constexpr absl::string_view TextReadouts = "TextReadouts";
} // namespace Labels

enum class StatsFormat {
  Json,
  Prometheus,
  Text,
};

struct StatsParams {
  /**
   * Parses the URL's query parameter, populating this.
   *
   * @param url the URL from which to parse the query params.
   * @param response used to write error messages, if necessary.
   */
  Http::Code parse(absl::string_view url, Buffer::Instance& response);

  bool used_only_{false};
  bool prometheus_text_readouts_{false};
  bool pretty_{false};
  StatsFormat format_{StatsFormat::Text};
  std::string filter_string_;
  absl::optional<std::regex> filter_;
  Utility::HistogramBucketsMode histogram_buckets_mode_{Utility::HistogramBucketsMode::NoBuckets};
  Http::Utility::QueryParams query_;
};

} // namespace Server
} // namespace Envoy
