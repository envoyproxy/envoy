#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/priority_load_shed/v3/priority_load_shed.pb.h"
#include "envoy/server/overload/load_shed_point.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"
#include "source/common/http/header_map_impl.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityLoadShed {

#define ALL_PRIORITY_LOAD_SHED_STATS(COUNTER)                                                      \
  COUNTER(total)                                                                                   \
  COUNTER(header_missing)                                                                          \
  COUNTER(header_invalid)                                                                          \
  COUNTER(bucket_unmatched)                                                                        \
  COUNTER(passed)                                                                                  \
  COUNTER(shed)

struct PriorityLoadShedStats {
  ALL_PRIORITY_LOAD_SHED_STATS(GENERATE_COUNTER_STRUCT)
};

class PriorityLoadShedFilterConfig : public Logger::Loggable<Logger::Id::config> {
public:
  using ProtoConfig = envoy::extensions::filters::http::priority_load_shed::v3::PriorityLoadShed;

  struct Bucket {
    int32_t start;
    int32_t end;
    std::string load_shed_point_name;
    Server::LoadShedPoint* load_shed_point;
  };

  static absl::StatusOr<std::shared_ptr<PriorityLoadShedFilterConfig>>
  create(const ProtoConfig& config, Server::LoadShedPointProvider& load_shed_point_provider,
         const std::string& stats_prefix, Stats::Scope& scope);

  const Http::LowerCaseString& headerName() const { return header_name_; }
  const Bucket* findBucket(int32_t value) const;
  bool hasDefaultLoadShedPoint() const { return !default_load_shed_point_name_.empty(); }
  Server::LoadShedPoint* defaultLoadShedPoint() const { return default_load_shed_point_; }
  PriorityLoadShedStats& stats() { return stats_; }

private:
  PriorityLoadShedFilterConfig(const ProtoConfig& config,
                               Server::LoadShedPointProvider& load_shed_point_provider,
                               const std::string& stats_prefix, Stats::Scope& scope,
                               absl::Status& creation_status);

  static PriorityLoadShedStats generateStats(const std::string& stats_prefix, Stats::Scope& scope);

  const Http::LowerCaseString header_name_;
  std::vector<Bucket> buckets_;
  std::string default_load_shed_point_name_;
  Server::LoadShedPoint* default_load_shed_point_{nullptr};
  PriorityLoadShedStats stats_;
};

class PriorityLoadShedFilter : public Http::PassThroughDecoderFilter {
public:
  explicit PriorityLoadShedFilter(std::shared_ptr<PriorityLoadShedFilterConfig> config)
      : config_(std::move(config)) {}

  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers, bool) override;

private:
  Http::FilterHeadersStatus rejectBadRequest(absl::string_view message, absl::string_view details);
  std::shared_ptr<PriorityLoadShedFilterConfig> config_;
};

} // namespace PriorityLoadShed
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
