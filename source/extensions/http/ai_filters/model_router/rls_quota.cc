#include "source/extensions/http/ai_filters/model_router/rls_quota.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/extensions/common/ratelimit/v3/ratelimit.pb.h"
#include "envoy/service/ratelimit/v3/rls.pb.h"
#include "envoy/type/v3/ratelimit_unit.pb.h"

#include "source/common/coroutine/status_macros.h"
#include "source/common/protobuf/utility.h"

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AiFilters {
namespace ModelRouter {

namespace {

namespace Config = envoy::extensions::http::ai_filters::model_router::v3;
using envoy::extensions::common::ratelimit::v3::RateLimitDescriptor;
using envoy::service::ratelimit::v3::RateLimitRequest;
using envoy::service::ratelimit::v3::RateLimitResponse;
using envoy::type::v3::RateLimitUnit;

constexpr absl::string_view ProviderEntryKey{"provider"};
constexpr absl::string_view ModelEntryKey{"model"};
constexpr absl::string_view QuotaTypeEntryKey{"quota_type"};
constexpr absl::string_view RequestsQuotaTypeValue{"requests"};
constexpr absl::string_view TokensQuotaTypeValue{"tokens"};

RateLimitUnit toRateLimitUnit(Config::TimeUnit time_unit) {
  switch (time_unit) {
  case Config::MINUTE:
    return envoy::type::v3::MINUTE;
  case Config::HOUR:
    return envoy::type::v3::HOUR;
  case Config::DAY:
    return envoy::type::v3::DAY;
  default:                          // LCOV_EXCL_LINE
    return envoy::type::v3::MINUTE; // LCOV_EXCL_LINE
  }
}

void addEntry(RateLimitDescriptor& descriptor, absl::string_view key, absl::string_view value) {
  auto* entry = descriptor.add_entries();
  entry->set_key(std::string(key));
  entry->set_value(std::string(value));
}

void addDescriptor(RateLimitRequest& request, absl::string_view provider, absl::string_view model,
                   absl::string_view quota_type, uint64_t hits_addend,
                   const RateLimitDescriptor::RateLimitOverride* override_limit = nullptr) {
  auto* descriptor = request.add_descriptors();
  addEntry(*descriptor, ProviderEntryKey, provider);
  addEntry(*descriptor, ModelEntryKey, model);
  addEntry(*descriptor, QuotaTypeEntryKey, quota_type);
  descriptor->mutable_hits_addend()->set_value(hits_addend);
  if (override_limit != nullptr) {
    *descriptor->mutable_limit() = *override_limit;
  }
}

} // namespace

RlsQuota::RlsQuota(const Config::ModelRouter& proto, const Config::RlsQuotaConfig& rls_config,
                   Grpc::RawAsyncClientSharedPtr async_client)
    : quotas_(buildQuotaMap(proto)),
      domain_(rls_config.domain().empty() ? std::string(DefaultDomain) : rls_config.domain()),
      client_(std::move(async_client),
              std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(
                  rls_config, timeout, AsyncRlsClient::DefaultTimeout.count()))) {}

std::shared_ptr<const RlsQuota::ModelQuotaMap>
RlsQuota::buildQuotaMap(const Config::ModelRouter& proto) {
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

      RateLimitOverride override_limit;
      override_limit.set_requests_per_unit(quota.limit());
      override_limit.set_unit(toRateLimitUnit(quota.time_unit()));
      const uint32_t multiplier = PROTOBUF_GET_WRAPPED_OR_DEFAULT(quota, unit_multiplier, 1);
      if (multiplier != 1) {
        override_limit.mutable_unit_multiplier()->set_value(multiplier);
      }

      switch (quota.quota_type()) {
      case Config::REQUESTS:
        model_quotas.request_overrides.push_back(std::move(override_limit));
        break;
      case Config::TOKENS:
        model_quotas.token_overrides.push_back(std::move(override_limit));
        break;
      default: // LCOV_EXCL_LINE
        break; // LCOV_EXCL_LINE
      }
    }
  }

  return map;
}

Coroutine::Task<absl::StatusOr<bool>> RlsQuota::checkLimits(std::string provider, std::string model,
                                                            uint64_t estimated_input_tokens) {
  const uint64_t required_tokens = std::max<uint64_t>(1, estimated_input_tokens);
  RateLimitRequest request;
  request.set_domain(domain_);

  const auto it = quotas_->find(ModelKey{provider, model});
  if (it != quotas_->end()) {
    const ModelQuotas& model_quotas = it->second;
    if (model_quotas.disabled) {
      co_return false;
    }
    for (const RateLimitOverride& override_limit : model_quotas.request_overrides) {
      addDescriptor(request, provider, model, RequestsQuotaTypeValue, 1, &override_limit);
    }
    for (const RateLimitOverride& override_limit : model_quotas.token_overrides) {
      addDescriptor(request, provider, model, TokensQuotaTypeValue, required_tokens,
                    &override_limit);
    }
  } else {
    addDescriptor(request, provider, model, RequestsQuotaTypeValue, 1);
    addDescriptor(request, provider, model, TokensQuotaTypeValue, required_tokens);
  }

  ASSIGN_OR_CO_RETURN(RateLimitResponse response, co_await client_.checkRateLimit(request));
  co_return response.overall_code() != RateLimitResponse::OVER_LIMIT;
}

Coroutine::Task<absl::Status> RlsQuota::consumeQuota(std::string provider, std::string model,
                                                     uint64_t input_tokens,
                                                     uint64_t output_tokens) {
  const uint64_t total_tokens = input_tokens + output_tokens;
  RateLimitRequest request;
  request.set_domain(domain_);

  const auto it = quotas_->find(ModelKey{provider, model});
  if (it != quotas_->end()) {
    const ModelQuotas& model_quotas = it->second;
    if (model_quotas.disabled) {
      co_return absl::OkStatus();
    }
    if (model_quotas.request_overrides.empty() &&
        (model_quotas.token_overrides.empty() || total_tokens == 0)) {
      co_return absl::OkStatus();
    }
    for (const RateLimitOverride& override_limit : model_quotas.request_overrides) {
      addDescriptor(request, provider, model, RequestsQuotaTypeValue, 1, &override_limit);
    }
    if (total_tokens > 0) {
      for (const RateLimitOverride& override_limit : model_quotas.token_overrides) {
        addDescriptor(request, provider, model, TokensQuotaTypeValue, total_tokens,
                      &override_limit);
      }
    }
  } else {
    addDescriptor(request, provider, model, RequestsQuotaTypeValue, 1);
    if (total_tokens > 0) {
      addDescriptor(request, provider, model, TokensQuotaTypeValue, total_tokens);
    }
  }

  CO_RETURN_IF_ERROR((co_await client_.consumeRateLimit(request)).status());
  co_return absl::OkStatus();
}

} // namespace ModelRouter
} // namespace AiFilters
} // namespace Extensions
} // namespace Envoy
