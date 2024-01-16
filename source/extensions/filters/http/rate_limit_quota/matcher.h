#pragma once

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ::envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings;
using ::envoy::service::rate_limit_quota::v3::BucketId;

class RateLimitQuotaValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Http::HttpMatchingData> {
public:
  // TODO(tyxia) Add actual validation later once CEL expression is added.
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

// Contextual information used to construct the onMatch actions for a match tree.
// Currently it is empty struct.
struct RateLimitOnMatchActionContext {};

// This class implements the on_match action behavior.
class RateLimitOnMatchAction : public Matcher::ActionBase<BucketId>,
                               public Logger::Loggable<Logger::Id::rate_limit_quota> {
public:
  explicit RateLimitOnMatchAction(RateLimitQuotaBucketSettings settings)
      : setting_(std::move(settings)) {}

  absl::StatusOr<BucketId> generateBucketId(const Http::Matching::HttpMatchingDataImpl& data,
                                            Server::Configuration::FactoryContext& factory_context,
                                            RateLimitQuotaValidationVisitor& visitor) const;

  const RateLimitQuotaBucketSettings& bucketSettings() const { return setting_; }

private:
  RateLimitQuotaBucketSettings setting_;
};

class RateLimitOnMatchActionFactory : public Matcher::ActionFactory<RateLimitOnMatchActionContext> {
public:
  std::string name() const override { return "rate_limit_quota"; }

  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config, RateLimitOnMatchActionContext&,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override {
    // Validate and then retrieve the bucket settings from config.
    const auto bucket_settings =
        MessageUtil::downcastAndValidate<const envoy::extensions::filters::http::rate_limit_quota::
                                             v3::RateLimitQuotaBucketSettings&>(config,
                                                                                validation_visitor);
    return [bucket_settings = std::move(bucket_settings)]() {
      return std::make_unique<RateLimitOnMatchAction>(std::move(bucket_settings));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings>();
  }
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
