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
  absl::Status creation_status = absl::OkStatus();
  auto config_ptr = std::shared_ptr<PriorityLoadShedFilterConfig>(new PriorityLoadShedFilterConfig(
      config, load_shed_point_provider, stats_prefix, scope, creation_status));
  if (!creation_status.ok()) {
    return creation_status;
  }
  return config_ptr;
}

PriorityLoadShedFilterConfig::PriorityLoadShedFilterConfig(
    const ProtoConfig& config, Server::LoadShedPointProvider& load_shed_point_provider,
    const std::string& stats_prefix, Stats::Scope& scope, absl::Status& creation_status)
    : header_name_(config.header_name()),
      reject_on_missing_header_(config.reject_on_missing_header()),
      reject_on_invalid_header_(config.reject_on_invalid_header()),
      stats_(generateStats(stats_prefix, scope)) {
  if (config.header_name().empty()) {
    creation_status = absl::InvalidArgumentError("header_name must be non-empty");
    return;
  }

  if (config.buckets().empty()) {
    creation_status = absl::InvalidArgumentError("at least one bucket must be configured");
    return;
  }

  buckets_.reserve(config.buckets().size());
  for (const auto& bucket_config : config.buckets()) {
    const auto& range = bucket_config.value_range();
    if (range.start() < 0 || range.end() <= range.start()) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("invalid bucket range [{}, {})", range.start(), range.end()));
      return;
    }

    Server::LoadShedPoint* load_shed_point =
        load_shed_point_provider.getLoadShedPoint(bucket_config.load_shed_point());
    if (load_shed_point == nullptr) {
      ENVOY_LOG(warn, "load shed point '{}' is not configured; matched requests will bypass",
                bucket_config.load_shed_point());
    }

    buckets_.push_back(
        Bucket{range.start(), range.end(), bucket_config.load_shed_point(), load_shed_point});
  }

  std::sort(buckets_.begin(), buckets_.end(),
            [](const Bucket& lhs, const Bucket& rhs) { return lhs.start < rhs.start; });

  for (size_t i = 1; i < buckets_.size(); ++i) {
    if (buckets_[i - 1].end > buckets_[i].start) {
      creation_status = absl::InvalidArgumentError(
          fmt::format("bucket ranges overlap: [{}, {}) and [{}, {})", buckets_[i - 1].start,
                      buckets_[i - 1].end, buckets_[i].start, buckets_[i].end));
      return;
    }
  }

  if (!config.default_load_shed_point().empty()) {
    default_load_shed_point_name_ = config.default_load_shed_point();
    default_load_shed_point_ =
        load_shed_point_provider.getLoadShedPoint(default_load_shed_point_name_);
    if (default_load_shed_point_ == nullptr) {
      ENVOY_LOG(warn,
                "default load shed point '{}' is not configured; fallback requests will bypass",
                default_load_shed_point_name_);
    }
  }
}

const PriorityLoadShedFilterConfig::Bucket*
PriorityLoadShedFilterConfig::findBucket(int64_t value) const {
  const auto it =
      std::upper_bound(buckets_.begin(), buckets_.end(), value,
                       [](int64_t target, const Bucket& bucket) { return target < bucket.start; });

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
  decoder_callbacks_->sendLocalReply(Http::Code::BadRequest, message, nullptr, absl::nullopt,
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
    if (config_->rejectOnMissingHeader()) {
      return rejectBadRequest("missing priority header", "priority_load_shed.missing_header");
    }
    if (!config_->hasDefaultLoadShedPoint()) {
      return Http::FilterHeadersStatus::Continue;
    }
    load_shed_point = config_->defaultLoadShedPoint();
  } else {
    int64_t header_value = 0;
    if (!absl::SimpleAtoi(header_values[0]->value().getStringView(), &header_value) ||
        header_value < 0) {
      config_->stats().header_invalid_.inc();
      if (config_->rejectOnInvalidHeader()) {
        return rejectBadRequest("invalid priority header", "priority_load_shed.invalid_header");
      }
      if (!config_->hasDefaultLoadShedPoint()) {
        return Http::FilterHeadersStatus::Continue;
      }
      load_shed_point = config_->defaultLoadShedPoint();
    } else {
      const auto* bucket = config_->findBucket(header_value);
      if (bucket != nullptr) {
        load_shed_point = bucket->load_shed_point;
      } else {
        config_->stats().bucket_unmatched_.inc();
        if (!config_->hasDefaultLoadShedPoint()) {
          return Http::FilterHeadersStatus::Continue;
        }
        load_shed_point = config_->defaultLoadShedPoint();
      }
    }
  }

  if (load_shed_point == nullptr) {
    config_->stats().bucket_unresolved_point_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  if (!load_shed_point->shouldShedLoad()) {
    config_->stats().passed_.inc();
    return Http::FilterHeadersStatus::Continue;
  }

  config_->stats().shed_.inc();
  decoder_callbacks_->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::OverloadManager);
  decoder_callbacks_->sendLocalReply(Http::Code::ServiceUnavailable, "envoy overloaded", nullptr,
                                     absl::nullopt,
                                     StreamInfo::ResponseCodeDetails::get().Overload);
  return Http::FilterHeadersStatus::StopIteration;
}

} // namespace PriorityLoadShed
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
