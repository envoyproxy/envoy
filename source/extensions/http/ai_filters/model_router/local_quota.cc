#include "source/extensions/http/ai_filters/model_router/local_quota.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

namespace {

namespace Config = envoy::extensions::http::ai_filters::model_router::v3;

double windowSeconds(Config::TimeUnit time_unit) {
  switch (time_unit) {
  case Config::MINUTE:
    return 60.0;
  case Config::HOUR:
    return 3600.0;
  case Config::DAY:
    return 86400.0;
  default:
    return 60.0;
  }
}

} // namespace

LocalQuota::LocalQuota(const Config::ModelRouter& proto, TimeSource& time_source)
    : quotas_(buildQuotaMap(proto, time_source)) {}

std::shared_ptr<const LocalQuota::ModelQuotaMap>
LocalQuota::buildQuotaMap(const Config::ModelRouter& proto, TimeSource& time_source) {
  auto map = std::make_shared<ModelQuotaMap>();

  for (const Config::Model& model_entry : proto.models()) {
    if (model_entry.quotas().empty()) {
      continue;
    }

    ModelQuotas& model_quotas = (*map)[ModelKey{model_entry.provider(), model_entry.model()}];
    for (const Config::Quota& quota : model_entry.quotas()) {
      const uint64_t limit = quota.limit();
      const double fill_rate = static_cast<double>(limit) / windowSeconds(quota.time_unit());
      auto bucket = std::make_unique<AtomicTokenBucketImpl>(limit, time_source, fill_rate);

      switch (quota.quota_type()) {
      case Config::REQUESTS:
        model_quotas.request_buckets.push_back(std::move(bucket));
        break;
      case Config::TOKENS:
        model_quotas.token_buckets.push_back(std::move(bucket));
        break;
      default:
        break;
      }
    }
  }

  return map;
}

Coroutine::Task<absl::StatusOr<bool>>
LocalQuota::checkLimits(std::string provider, std::string model, uint64_t estimated_input_tokens) {
  const auto it = quotas_->find(ModelKey{std::move(provider), std::move(model)});
  if (it == quotas_->end()) {
    co_return true;
  }

  const ModelQuotas& model_quotas = it->second;
  for (const auto& bucket : model_quotas.request_buckets) {
    if (bucket->remainingTokens() < 1.0) {
      co_return false;
    }
  }

  const double required_tokens = static_cast<double>(std::max<uint64_t>(1, estimated_input_tokens));
  for (const auto& bucket : model_quotas.token_buckets) {
    if (bucket->remainingTokens() < required_tokens) {
      co_return false;
    }
  }

  co_return true;
}

Coroutine::Task<absl::Status> LocalQuota::consumeQuota(std::string provider, std::string model,
                                                       uint64_t input_tokens,
                                                       uint64_t output_tokens) {
  const auto it = quotas_->find(ModelKey{std::move(provider), std::move(model)});
  if (it == quotas_->end()) {
    co_return absl::OkStatus();
  }

  const ModelQuotas& model_quotas = it->second;
  for (const auto& bucket : model_quotas.request_buckets) {
    if (bucket->maxTokens() > 0.0) {
      bucket->consume([](double) { return 1.0; });
    }
  }

  const double total_tokens =
      static_cast<double>(input_tokens) + static_cast<double>(output_tokens);
  if (total_tokens > 0.0) {
    for (const auto& bucket : model_quotas.token_buckets) {
      if (bucket->maxTokens() > 0.0) {
        bucket->consume([total_tokens](double) { return total_tokens; });
      }
    }
  }

  co_return absl::OkStatus();
}

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
