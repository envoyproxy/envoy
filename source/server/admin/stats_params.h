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

  template <class StatType>
  using CallOnStatFn =
      std::function<void(const Stats::RefcountPtr<StatType>& stat, const std::string& name)>;

  /*
   * Determine whether a metric has never been emitted and choose to
   * not show it if we only wanted used metrics.
   */
  template <class StatType>
  void callIfShouldShowStat(const Stats::RefcountPtr<StatType>& stat,
                            CallOnStatFn<StatType> call_on_stat_fn) const {
    if (used_only_ && !stat->used()) {
      return;
    }
    std::string name = stat->name();
    if (safe_filter_ != nullptr && !re2::RE2::PartialMatch(name, *safe_filter_)) {
      return;
    } else if (filter_.has_value() && !std::regex_search(name, filter_.value())) {
      return;
    }
    call_on_stat_fn(stat, name);
  }

  bool used_only_{false};
  bool prometheus_text_readouts_{false};
  bool pretty_{false};
  StatsFormat format_{StatsFormat::Text};
  std::string filter_string_;
  absl::optional<std::regex> filter_;
  std::shared_ptr<re2::RE2> safe_filter_;
  Utility::HistogramBucketsMode histogram_buckets_mode_{Utility::HistogramBucketsMode::NoBuckets};
  Http::Utility::QueryParams query_;
};

} // namespace Server
} // namespace Envoy
