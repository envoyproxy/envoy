#pragma once

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/matcher/matcher.h"

#include "absl/container/flat_hash_map.h"

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
  // Data inputs for the bucket id builders that use custom_value, keyed by bucket id key.
  using CustomValueInputs =
      absl::flat_hash_map<std::string, Matcher::DataInputPtr<Http::HttpMatchingData>>;

  RateLimitOnMatchAction(RateLimitQuotaBucketSettings settings,
                         CustomValueInputs custom_value_inputs)
      : setting_(std::move(settings)), custom_value_inputs_(std::move(custom_value_inputs)) {}

  // Creates the data inputs for the custom_value bucket id builders in the settings. This parses
  // the input configurations, so it runs when the action is created rather than per request.
  static CustomValueInputs
  createCustomValueInputs(const RateLimitQuotaBucketSettings& settings,
                          ProtobufMessage::ValidationVisitor& validation_visitor);

  absl::StatusOr<BucketId> generateBucketId(const Http::Matching::HttpMatchingDataImpl& data) const;

  const RateLimitQuotaBucketSettings& bucketSettings() const { return setting_; }

private:
  const RateLimitQuotaBucketSettings setting_;
  const CustomValueInputs custom_value_inputs_;
};

class RateLimitOnMatchActionFactory : public Matcher::ActionFactory<RateLimitOnMatchActionContext> {
public:
  std::string name() const override { return "rate_limit_quota"; }

  Matcher::ActionConstSharedPtr
  createAction(const Protobuf::Message& config, RateLimitOnMatchActionContext&,
               ProtobufMessage::ValidationVisitor& validation_visitor) override {
    // Validate and then retrieve the bucket settings from config.
    const auto& bucket_settings =
        MessageUtil::downcastAndValidate<const envoy::extensions::filters::http::rate_limit_quota::
                                             v3::RateLimitQuotaBucketSettings&>(config,
                                                                                validation_visitor);
    return std::make_shared<RateLimitOnMatchAction>(
        bucket_settings,
        RateLimitOnMatchAction::createCustomValueInputs(bucket_settings, validation_visitor));
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
