#include "source/extensions/filters/http/priority_load_shed/filter.h"

#include <algorithm>
#include <string>

#include "envoy/http/codes.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/fmt.h"
#include "source/common/http/headers.h"
#include "source/common/stream_info/utility.h"

#include "absl/strings/numbers.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PriorityLoadShed {

absl::StatusOr<std::shared_ptr<PriorityLoadShedFilterConfig>>
PriorityLoadShedFilterConfig::create(const ProtoConfig& config,
                                     Server::LoadShedPointProvider& load_shed_point_provider,
                                     const std::string& stats_prefix, Stats::Scope& scope) {
  if (config.header_name().empty()) {
    return absl::InvalidArgumentError("header_name must be non-empty");
  }

  if (config.buckets().empty()) {
    return absl::InvalidArgumentError("at least one bucket must be configured");
  }

  std::vector<Bucket> buckets;
  buckets.reserve(config.buckets().size());
  for (const auto& bucket_config : config.buckets()) {
    const auto& range = bucket_config.value_range();
    if (range.start() < 0 || range.end() <= range.start()) {
      return absl::InvalidArgumentError(
          fmt::format("invalid bucket range [{}, {})", range.start(), range.end()));
    }

    Server::LoadShedPoint* load_shed_point =
        load_shed_point_provider.getLoadShedPoint(bucket_config.load_shed_point());
    if (load_shed_point == nullptr) {
      return absl::InvalidArgumentError(
          fmt::format("load shed point '{}' is not configured in the overload manager",
                      bucket_config.load_shed_point()));
    }

    buckets.push_back(
        Bucket{range.start(), range.end(), bucket_config.load_shed_point(), load_shed_point});
  }

  std::sort(buckets.begin(), buckets.end(),
            [](const Bucket& lhs, const Bucket& rhs) { return lhs.start < rhs.start; });

  for (size_t i = 1; i < buckets.size(); ++i) {
    if (buckets[i - 1].end > buckets[i].start) {
      return absl::InvalidArgumentError(
          fmt::format("bucket ranges overlap: [{}, {}) and [{}, {})", buckets[i - 1].start,
                      buckets[i - 1].end, buckets[i].start, buckets[i].end));
    }
  }

  std::string default_load_shed_point_name;
  Server::LoadShedPoint* default_load_shed_point = nullptr;
  if (!config.default_load_shed_point().empty()) {
    default_load_shed_point_name = config.default_load_shed_point();
    default_load_shed_point =
        load_shed_point_provider.getLoadShedPoint(default_load_shed_point_name);
    if (default_load_shed_point == nullptr) {
      return absl::InvalidArgumentError(
          fmt::format("default load shed point '{}' is not configured in the overload manager",
                      default_load_shed_point_name));
    }
  }

  return std::shared_ptr<PriorityLoadShedFilterConfig>(new PriorityLoadShedFilterConfig(
      Http::LowerCaseString(config.header_name()), std::move(buckets),
      std::move(default_load_shed_point_name), default_load_shed_point,
      stats_prefix, scope));
}

PriorityLoadShedFilterConfig::PriorityLoadShedFilterConfig(
    Http::LowerCaseString header_name, std::vector<Bucket> buckets,
    std::string default_load_shed_point_name, Server::LoadShedPoint* default_load_shed_point,
    const std::string& stats_prefix, Stats::Scope& scope)
    : header_name_(std::move(header_name)), buckets_(std::move(buckets)),
      default_load_shed_point_name_(std::move(default_load_shed_point_name)),
      default_load_shed_point_(default_load_shed_point),
      stats_(generateStats(stats_prefix, scope)) {}

const PriorityLoadShedFilterConfig::Bucket*
PriorityLoadShedFilterConfig::findBucket(int32_t value) const {
  const auto it =
      std::upper_bound(buckets_.begin(), buckets_.end(), value,
                       [](int32_t target, const Bucket& bucket) { return target < bucket.start; });

  if (it == buckets_.begin()) {
    return nullptr;
  }

  const auto candidate = std::prev(it);
  return value < candidate->end ? &(*candidate) : nullptr;
}

PriorityLoadShedStats PriorityLoadShedFilterConfig::generateStats(const std::string& stats_prefix,
                                                                  Stats::Scope& scope) {
  const std::string prefix = stats_prefix + "priority_load_shed.";
  return {ALL_PRIORITY_LOAD_SHED_STATS(POOL_COUNTER_PREFIX(scope, prefix))};
}

Http::FilterHeadersStatus PriorityLoadShedFilter::rejectBadRequest(absl::string_view message,
                                                                   absl::string_view details) {
  decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, message, nullptr, std::nullopt,
                                     details);
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterHeadersStatus PriorityLoadShedFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                                bool) {
  config_->stats().total_.inc();

  Server::LoadShedPoint* load_shed_point = nullptr;
  const auto header_values = headers.get(config_->headerName());
  if (header_values.empty()) {
    config_->stats().header_missing_.inc();
    if (!config_->hasDefaultLoadShedPoint()) {
      return rejectBadRequest("missing priority header", "priority_load_shed.missing_header");
    }
    load_shed_point = config_->defaultLoadShedPoint();
  } else {
    int32_t header_value = 0;
    if (!absl::SimpleAtoi(header_values[0]->value().getStringView(), &header_value) ||
        header_value < 0) {
      config_->stats().header_invalid_.inc();
      if (!config_->hasDefaultLoadShedPoint()) {
        return rejectBadRequest("invalid priority header", "priority_load_shed.invalid_header");
      }
      load_shed_point = config_->defaultLoadShedPoint();
    } else {
      const auto* bucket = config_->findBucket(header_value);
      if (bucket != nullptr) {
        load_shed_point = bucket->load_shed_point;
      } else {
        config_->stats().bucket_unmatched_.inc();
        if (!config_->hasDefaultLoadShedPoint()) {
          return rejectBadRequest("priority value does not match any configured bucket",
                                  "priority_load_shed.bucket_unmatched");
        }
        load_shed_point = config_->defaultLoadShedPoint();
      }
    }
  }

  if (!load_shed_point->shouldShedLoad()) {
    config_->stats().passed_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  config_->stats().shed_.inc();
  decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::OverloadManager);
  decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "envoy overloaded", nullptr,
                                     std::nullopt, StreamInfo::ResponseCodeDetails::get().Overload);
  return Http::FilterHeadersStatus::StopIteration;
}

} // namespace PriorityLoadShed
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
