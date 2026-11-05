#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"

#include "source/common/coroutine/task.h"
#include "source/extensions/http/ai_filters/model_router/async_redis.h"
#include "source/extensions/http/ai_filters/model_router/quota_interface.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

class RedisQuota : public QuotaInterface {
public:
  static constexpr int DefaultPort = 6379;

  RedisQuota(
      const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto,
      const envoy::extensions::http::ai_filters::model_router::v3::RedisQuotaConfig& redis_config,
      Event::Dispatcher& dispatcher, TimeSource& time_source);
  ~RedisQuota() override = default;

  Coroutine::Task<absl::StatusOr<bool>> checkLimits(std::string provider, std::string model,
                                                    uint64_t estimated_input_tokens) override;

  Coroutine::Task<absl::Status> consumeQuota(std::string provider, std::string model,
                                             uint64_t input_tokens,
                                             uint64_t output_tokens) override;

private:
  struct ModelKey {
    std::string provider;
    std::string model;

    bool operator==(const ModelKey& other) const {
      return provider == other.provider && model == other.model;
    }

    template <typename H> friend H AbslHashValue(H h, const ModelKey& key) {
      return H::combine(std::move(h), key.provider, key.model);
    }
  };

  struct QuotaSpec {
    uint64_t limit{0};
    uint64_t window_seconds{0};
    uint64_t bucket_seconds{0};
    size_t num_buckets{0};
    std::string key_prefix;
  };

  struct ModelQuotas {
    bool disabled{false};
    std::vector<QuotaSpec> request_quotas;
    std::vector<QuotaSpec> token_quotas;
  };

  using ModelQuotaMap = absl::flat_hash_map<ModelKey, ModelQuotas>;

  static std::shared_ptr<const ModelQuotaMap>
  buildQuotaMap(const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto);

  Coroutine::Task<absl::StatusOr<AsyncRedis*>> ensureConnected();

  std::shared_ptr<const ModelQuotaMap> quotas_;
  Event::Dispatcher& dispatcher_;
  TimeSource& time_source_;
  std::string host_;
  int port_{DefaultPort};
  std::chrono::milliseconds timeout_{AsyncRedis::DefaultTimeout};

  std::unique_ptr<AsyncRedis> redis_;
  bool connecting_{false};
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
