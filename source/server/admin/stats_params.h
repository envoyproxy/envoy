#pragma once

#include <memory>
#include <regex>
#include <string>

#include "envoy/buffer/buffer.h"
#include "envoy/http/codes.h"

#include "source/server/admin/utils.h"

#include "re2/re2.h"

namespace Envoy {
namespace Server {

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
  std::shared_ptr<std::regex> filter_;
  std::shared_ptr<re2::RE2> re2_filter_;
  Utility::HistogramBucketsMode histogram_buckets_mode_{Utility::HistogramBucketsMode::NoBuckets};
  Http::Utility::QueryParams query_;
};

} // namespace Server
} // namespace Envoy
