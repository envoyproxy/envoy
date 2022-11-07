#pragma once
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/matcher/matcher.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/rate_limit_quota/client.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using FilterConfig =
    envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaFilterConfig;
using FilterConfigConstSharedPtr = std::shared_ptr<const FilterConfig>;
using ValueSpecifierCase = ::envoy::extensions::filters::http::rate_limit_quota::v3::
    RateLimitQuotaBucketSettings_BucketIdBuilder_ValueBuilder::ValueSpecifierCase;
using BucketId = ::envoy::service::rate_limit_quota::v3::BucketId;
using QuotaAssignmentAction = ::envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse::
    BucketAction::QuotaAssignmentAction;

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
struct RateLimitOnMactchActionContext {};

// This class implements the on_match action behavior.
class RateLimitOnMactchAction : public Matcher::ActionBase<BucketId>,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  explicit RateLimitOnMactchAction(
      envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings settings)
      : setting_(std::move(settings)) {}

  absl::StatusOr<BucketId> generateBucketId(const Http::Matching::HttpMatchingDataImpl& data,
                                            Server::Configuration::FactoryContext& factory_context,
                                            RateLimitQuotaValidationVisitor& visitor) const;

private:
  envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings setting_;
};

class RateLimitOnMactchActionFactory
    : public Matcher::ActionFactory<RateLimitOnMactchActionContext> {
public:
  std::string name() const override { return "rate_limit_quota"; }

  Matcher::ActionFactoryCb
  createActionFactoryCb(const Protobuf::Message& config, RateLimitOnMactchActionContext&,
                        ProtobufMessage::ValidationVisitor& validation_visitor) override {
    // Validate and then retrieve the bucket settings from config.
    const auto bucket_settings =
        MessageUtil::downcastAndValidate<const envoy::extensions::filters::http::rate_limit_quota::
                                             v3::RateLimitQuotaBucketSettings&>(config,
                                                                                validation_visitor);
    return [bucket_settings = std::move(bucket_settings)]() {
      return std::make_unique<RateLimitOnMactchAction>(std::move(bucket_settings));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings>();
  }
};

class RateLimitQuotaFilter : public Http::PassThroughFilter,
                             public RateLimitQuotaCallbacks,
                             public Logger::Loggable<Logger::Id::filter> {
public:
  RateLimitQuotaFilter(FilterConfigConstSharedPtr config,
                       Server::Configuration::FactoryContext& factory_context)
      : config_(std::move(config)), factory_context_(factory_context) {
    createMatcher();
  }

  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  void onDestroy() override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  // RateLimitQuota::RateLimitQuotaCallbacks
  void onQuotaResponse(envoy::service::rate_limit_quota::v3::RateLimitQuotaResponse&) override {}

  // Perform request matching. It returns the generated bucket ids if the matching succeeded and
  // returns the error status otherwise.
  absl::StatusOr<BucketId> requestMatching(const Http::RequestHeaderMap& headers);

  ~RateLimitQuotaFilter() override = default;

private:
  // Create the matcher factory and matcher.
  void createMatcher();

  FilterConfigConstSharedPtr config_;
  Server::Configuration::FactoryContext& factory_context_;
  Http::StreamDecoderFilterCallbacks* callbacks_ = nullptr;
  RateLimitQuotaValidationVisitor visitor_ = {};
  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> matcher_ = nullptr;
  std::unique_ptr<Http::Matching::HttpMatchingDataImpl> data_ptr_ = nullptr;

  // Customized hash and equal struct for `BucketId` hash key.
  struct BucketIdHash {
    size_t operator()(const BucketId& bucket_id) const { return MessageUtil::hash(bucket_id); }
  };

  struct BucketIdEqual {
    bool operator()(const BucketId& id1, const BucketId& id2) const {
      return Protobuf::util::MessageDifferencer::Equals(id1, id2);
    }
  };
  // TODO(tyxia) Thread local storage.
  absl::node_hash_map<BucketId, QuotaAssignmentAction, BucketIdHash, BucketIdEqual>
      quota_assignment_map_;
};

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
