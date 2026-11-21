#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/extensions/http/ai_filters/model_router/v3/model_router.pb.h"
#include "envoy/grpc/async_client.h"

#include "source/common/coroutine/task.h"
#include "source/extensions/http/ai_filters/model_router/async_rls.h"
#include "source/extensions/http/ai_filters/model_router/quota_interface.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

class RlsQuota : public QuotaInterface {
public:
  static constexpr absl::string_view DefaultDomain{"model_router"};

  RlsQuota(const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto,
           const envoy::extensions::http::ai_filters::model_router::v3::RlsQuotaConfig& rls_config,
           Grpc::RawAsyncClientSharedPtr async_client);
  ~RlsQuota() override = default;

  Coroutine::Task<absl::StatusOr<bool>> checkLimits(std::string provider, std::string model,
                                                    uint64_t estimated_input_tokens) override;

  Coroutine::Task<absl::Status> consumeQuota(std::string provider, std::string model,
                                             uint64_t input_tokens,
                                             uint64_t output_tokens) override;

private:
  using RateLimitOverride =
      envoy::extensions::common::ratelimit::v3::RateLimitDescriptor::RateLimitOverride;

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
    bool disabled{false};
    std::vector<RateLimitOverride> request_overrides;
    std::vector<RateLimitOverride> token_overrides;
  };

  using ModelQuotaMap = absl::flat_hash_map<ModelKey, ModelQuotas>;

  static std::shared_ptr<const ModelQuotaMap>
  buildQuotaMap(const envoy::extensions::http::ai_filters::model_router::v3::ModelRouter& proto);

  std::shared_ptr<const ModelQuotaMap> quotas_;
  std::string domain_;
  AsyncRlsClient client_;
};

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
