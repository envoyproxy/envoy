#include "source/extensions/http/ai_filters/model_router/redis_quota.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/common/cleanup.h"
#include "source/common/coroutine/status_macros.h"
#include "source/common/protobuf/utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

namespace {

namespace Config = envoy::extensions::http::ai_filters::model_router::v3;

struct WindowParams {
  uint64_t window_seconds;
  uint64_t bucket_seconds;
  size_t num_buckets;
  absl::string_view unit_tag;
};

WindowParams windowParams(Config::TimeUnit time_unit) {
  switch (time_unit) {
  case Config::MINUTE:
    return WindowParams{60, 1, 60, "m"};
  case Config::HOUR:
    return WindowParams{3600, 60, 60, "h"};
  case Config::DAY:
    return WindowParams{86400, 900, 96, "d"};
  default:
    return WindowParams{60, 1, 60, "m"};
  }
}

} // namespace

RedisQuota::RedisQuota(const Config::ModelRouter& proto, Event::Dispatcher& dispatcher,
                       TimeSource& time_source)
    : quotas_(buildQuotaMap(proto)), dispatcher_(dispatcher), time_source_(time_source),
      host_(proto.redis().host()),
      port_(proto.redis().port() != 0 ? static_cast<int>(proto.redis().port()) : DefaultPort),
      timeout_(std::chrono::milliseconds(
          PROTOBUF_GET_MS_OR_DEFAULT(proto.redis(), timeout, AsyncRedis::DefaultTimeout.count()))) {
}

std::shared_ptr<const RedisQuota::ModelQuotaMap>
RedisQuota::buildQuotaMap(const Config::ModelRouter& proto) {
  auto map = std::make_shared<ModelQuotaMap>();

  for (const Config::Model& model_entry : proto.models()) {
    if (model_entry.quotas().empty()) {
      continue;
    }

    ModelQuotas& model_quotas = (*map)[ModelKey{model_entry.provider(), model_entry.model()}];
    for (const Config::Quota& quota : model_entry.quotas()) {
      if (quota.limit() == 0) {
        model_quotas.disabled = true;
      }

      const WindowParams params = windowParams(quota.time_unit());
      switch (quota.quota_type()) {
      case Config::REQUESTS:
        model_quotas.request_quotas.push_back(QuotaSpec{
            quota.limit(),
            params.window_seconds,
            params.bucket_seconds,
            params.num_buckets,
            absl::StrCat("model_router:", model_entry.provider(), ":", model_entry.model(),
                         ":req:", params.unit_tag, ":"),
        });
        break;
      case Config::TOKENS:
        model_quotas.token_quotas.push_back(QuotaSpec{
            quota.limit(),
            params.window_seconds,
            params.bucket_seconds,
            params.num_buckets,
            absl::StrCat("model_router:", model_entry.provider(), ":", model_entry.model(),
                         ":tok:", params.unit_tag, ":"),
        });
        break;
      default:
        break;
      }
    }
  }

  return map;
}

Coroutine::Task<absl::StatusOr<AsyncRedis*>> RedisQuota::ensureConnected() {
  if (redis_ != nullptr && redis_->isConnected()) {
    co_return redis_.get();
  }
  // Only block one request while waiting for Redis connection. Fail-open for the rest.
  if (connecting_) {
    co_return absl::UnavailableError("Redis connection is in progress");
  }

  connecting_ = true;
  Cleanup guard([this]() { connecting_ = false; });

  ASSIGN_OR_CO_RETURN(std::unique_ptr<AsyncRedis> client,
                      AsyncRedis::open(dispatcher_, host_, port_, timeout_));
  CO_RETURN_IF_ERROR(co_await client->connect());

  redis_ = std::move(client);
  co_return redis_.get();
}

Coroutine::Task<absl::StatusOr<bool>>
RedisQuota::checkLimits(std::string provider, std::string model, uint64_t estimated_input_tokens) {
  const auto it = quotas_->find(ModelKey{std::move(provider), std::move(model)});
  if (it == quotas_->end()) {
    co_return true;
  }

  const ModelQuotas& model_quotas = it->second;
  if (model_quotas.disabled) {
    co_return false;
  }

  ASSIGN_OR_CO_RETURN(AsyncRedis * client, co_await ensureConnected());

  const int64_t now_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(time_source_.systemTime().time_since_epoch())
          .count();

  size_t total_keys = 0;
  for (const QuotaSpec& spec : model_quotas.request_quotas) {
    total_keys += spec.num_buckets;
  }
  for (const QuotaSpec& spec : model_quotas.token_quotas) {
    total_keys += spec.num_buckets;
  }

  std::vector<std::string> keys;
  keys.reserve(total_keys);

  auto append_keys = [&keys, now_seconds](const std::vector<QuotaSpec>& specs) {
    for (const QuotaSpec& spec : specs) {
      const int64_t current_bucket = now_seconds / static_cast<int64_t>(spec.bucket_seconds);
      for (size_t i = 0; i < spec.num_buckets; ++i) {
        keys.push_back(absl::StrCat(spec.key_prefix, current_bucket - static_cast<int64_t>(i)));
      }
    }
  };
  append_keys(model_quotas.request_quotas);
  append_keys(model_quotas.token_quotas);

  ASSIGN_OR_CO_RETURN(std::vector<int64_t> values, co_await client->mget(keys));

  size_t offset = 0;

  for (const QuotaSpec& spec : model_quotas.request_quotas) {
    int64_t used = 0;
    for (size_t i = 0; i < spec.num_buckets; ++i) {
      used += values[offset + i];
    }
    offset += spec.num_buckets;
    if (used + 1 > static_cast<int64_t>(spec.limit)) {
      co_return false;
    }
  }

  const int64_t required_tokens =
      static_cast<int64_t>(std::max<uint64_t>(1, estimated_input_tokens));
  for (const QuotaSpec& spec : model_quotas.token_quotas) {
    int64_t used = 0;
    for (size_t i = 0; i < spec.num_buckets; ++i) {
      used += values[offset + i];
    }
    offset += spec.num_buckets;
    if (used + required_tokens > static_cast<int64_t>(spec.limit)) {
      co_return false;
    }
  }

  co_return true;
}

Coroutine::Task<absl::Status> RedisQuota::consumeQuota(std::string provider, std::string model,
                                                       uint64_t input_tokens,
                                                       uint64_t output_tokens) {
  const auto it = quotas_->find(ModelKey{std::move(provider), std::move(model)});
  if (it == quotas_->end()) {
    co_return absl::OkStatus();
  }

  const ModelQuotas& model_quotas = it->second;
  if (model_quotas.disabled) {
    co_return absl::OkStatus();
  }

  const uint64_t total_tokens = input_tokens + output_tokens;
  if (model_quotas.request_quotas.empty() &&
      (model_quotas.token_quotas.empty() || total_tokens == 0)) {
    co_return absl::OkStatus();
  }

  ASSIGN_OR_CO_RETURN(AsyncRedis * client, co_await ensureConnected());

  const int64_t now_seconds =
      std::chrono::duration_cast<std::chrono::seconds>(time_source_.systemTime().time_since_epoch())
          .count();

  // Once coroutine library has "all_of" primitive, parallelize all `incrBy` calls, followed by
  // `expire` calls.
  for (const QuotaSpec& spec : model_quotas.request_quotas) {
    const int64_t current_bucket = now_seconds / static_cast<int64_t>(spec.bucket_seconds);
    const std::string key = absl::StrCat(spec.key_prefix, current_bucket);

    CO_RETURN_IF_ERROR((co_await client->incrBy(key, 1)).status());
    CO_RETURN_IF_ERROR(co_await client->expire(key, spec.window_seconds));
  }

  if (total_tokens > 0) {
    for (const QuotaSpec& spec : model_quotas.token_quotas) {
      const int64_t current_bucket = now_seconds / static_cast<int64_t>(spec.bucket_seconds);
      const std::string key = absl::StrCat(spec.key_prefix, current_bucket);

      CO_RETURN_IF_ERROR(
          (co_await client->incrBy(key, static_cast<int64_t>(total_tokens))).status());
      CO_RETURN_IF_ERROR(co_await client->expire(key, spec.window_seconds));
    }
  }

  co_return absl::OkStatus();
}

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
