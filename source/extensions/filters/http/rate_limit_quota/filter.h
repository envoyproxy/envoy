#pragma once
#include <memory>

#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.h"
#include "envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.pb.validate.h"

#include "source/common/http/matching/data_impl.h"
#include "source/common/http/message_impl.h"
#include "source/common/matcher/matcher.h"
#include "source/extensions/filters/http/common/factory_base.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"
#include "source/extensions/filters/http/rate_limit_quota/client_impl.h"

// TODO(tyxia) Needed for bucket id, re-evaluate later.
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.validate.h"
#include "envoy/registry/registry.h"

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

/**
 * TODO(tyxia) Placeholder Implement as needed for the interaction with filter.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;
};

class RateLimitQuotaValidationVisitor
    : public Matcher::MatchTreeValidationVisitor<Http::HttpMatchingData> {
public:
  // TODO(tyxia) Add validation later once CEL expression is added.
  absl::Status performDataInputValidation(const Matcher::DataInputFactory<Http::HttpMatchingData>&,
                                          absl::string_view) override {
    return absl::OkStatus();
  }
};

// TODO(tyxia) Starts with passThroughFilter consider streamFilter.
class RateLimitQuotaFilter : public Http::PassThroughFilter,
                             public RequestCallbacks,
                             public Logger::Loggable<Logger::Id::filter> {
public:
  RateLimitQuotaFilter(FilterConfigConstSharedPtr config,
                       Server::Configuration::FactoryContext& factory_context,
                       RateLimitClientPtr client)
      : config_(std::move(config)), rate_limit_client_(std::move(client)),
        factory_context_(factory_context) {
    createMatcher();
  }

  // Http::PassThroughDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap&, bool) override;
  void onDestroy() override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override;

  BucketId buildMatcherTree(const Http::RequestHeaderMap& headers);
  BucketId requestMatching(const Http::RequestHeaderMap& headers);

  ~RateLimitQuotaFilter() override = default;

private:
  // Create the matcher factory and matcher.
  void createMatcher();
  FilterConfigConstSharedPtr config_;
  RateLimitClientPtr rate_limit_client_;
  Server::Configuration::FactoryContext& factory_context_;
  Http::StreamDecoderFilterCallbacks* callbacks_;
  RateLimitQuotaValidationVisitor visitor_ = {};
  Matcher::MatchTreeSharedPtr<Http::HttpMatchingData> matcher_;
  std::unique_ptr<Http::Matching::HttpMatchingDataImpl> data_ptr_ = nullptr;
};

// TODO(tyxia) CEL expression.
// C++ exception with description "Didn't find a registered implementation for 'test_action' with
// type URL: 'envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings'"
// thrown in the test body.
// Looks like I need to implement the action factory and register it statically.
// 1. use action to create the matcher tree
// 2. use test action in the test to trigger the action???

// Contextual information used to construct the onMatch actions for a match tree.
// Currently it is empty struct.
struct RateLimitOnMactchActionContext {
  // RateLimitQuotaValidationVisitor visitor;
  // Server::Configuration::FactoryContext& factory_context;
  // Http::Matching::HttpMatchingDataImpl data;
  // Network::ConnectionInfoProvider& provider;
};

// This class implements the on_match action behavior.
class RateLimitOnMactchAction : public Matcher::ActionBase<BucketId>,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  // TODO(tyxia) Efficient coding pass by ref, capture etc.
  explicit RateLimitOnMactchAction(
      envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings settings)
      : setting_(std::move(settings)) {}

  BucketId generateBucketId(const Http::Matching::HttpMatchingDataImpl& data,
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

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
