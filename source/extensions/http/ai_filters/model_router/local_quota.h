#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"

#include "source/common/common/token_bucket_impl.h"
#include "source/common/coroutine/task.h"
#include "source/extensions/http/ai_filters/model_router/quota_interface.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

class LocalQuota : public QuotaInterface {
public:
  LocalQuota(const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto,
             TimeSource& time_source);
  LocalQuota(const LocalQuota&) = default;
  ~LocalQuota() override = default;

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

  struct ModelQuotas {
    std::vector<std::unique_ptr<AtomicTokenBucketImpl>> request_buckets;
    std::vector<std::unique_ptr<AtomicTokenBucketImpl>> token_buckets;
  };

  using ModelQuotaMap = absl::flat_hash_map<ModelKey, ModelQuotas>;

  static std::shared_ptr<const ModelQuotaMap>
  buildQuotaMap(const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto,
                TimeSource& time_source);

  std::shared_ptr<const ModelQuotaMap> quotas_;
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
