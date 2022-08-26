#include "source/extensions/filters/http/rate_limit_quota/filter.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ValueSpecifierCase = ::envoy::extensions::filters::http::rate_limit_quota::v3::
    RateLimitQuotaBucketSettings_BucketIdBuilder_ValueBuilder::ValueSpecifierCase;
using envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings;

// TODO(tyxia) buildBuckets might be the part of filter rather than the client.
// * the data plane sends a usage report for requests matched into the bucket with ``BucketId``
//   to the control plane
// * the control plane sends an assignment for the bucket with ``BucketId`` to the data plane
//   Bucket ID.
// https://github.com/envoyproxy/envoy/blob/9dfd549158665d403c7de9e0079a0759be44ed74/api/envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.proto#L64
// TODO(tyxia) bucksettings should be resovled from the on_match action.
BucketId RateLimitQuotaFilter::buildBucketsId(const Http::RequestHeaderMap& headers,
                                              const RateLimitQuotaBucketSettings& settings) {
  BucketId bucket_id;
  std::unique_ptr<Matcher::MatchInputFactory<Http::HttpMatchingData>> input_factory_ptr = nullptr;
  std::unique_ptr<Http::Matching::HttpMatchingDataImpl> data_ptr = nullptr;
  bool is_inited = false;
  for (auto id_builder : settings.bucket_id_builder().bucket_id_builder()) {
    std::string bucket_id_key = id_builder.first;
    auto builder_method = id_builder.second;

    switch (builder_method.value_specifier_case()) {
    // Retrieve the static string value directly from the config.
    case ValueSpecifierCase::kStringValue: {
      bucket_id.mutable_bucket()->insert({bucket_id_key, builder_method.string_value()});
      break;
    }
    // Retrieve the dynamic value from the `custom_value` typed extension config.
    case ValueSpecifierCase::kCustomValue: {
      if (!is_inited) {
        input_factory_ptr = std::make_unique<Matcher::MatchInputFactory<Http::HttpMatchingData>>(
            factory_context_.messageValidationVisitor(), visitor_);
        data_ptr = std::make_unique<Http::Matching::HttpMatchingDataImpl>(
            callbacks_->streamInfo().downstreamAddressProvider());
        is_inited = true;
      }
      // Create `DataInput` factory callback from the config.
      Matcher::DataInputFactoryCb<Http::HttpMatchingData> data_input_cb =
          input_factory_ptr->createDataInput(builder_method.custom_value());
      data_ptr->onRequestHeaders(headers);
      auto result = data_input_cb()->get(*data_ptr);
      // If result has data.
      if (result.data_) {
        if (!result.data_.value().empty()) {
          bucket_id.mutable_bucket()->insert({bucket_id_key, result.data_.value()});
        } else {
          ENVOY_LOG(debug, "Empty custom value");
        }
      } else {
        // TODO(tyxia) maybe add stats later
        ENVOY_LOG(debug, "Failed to retrive the result from custom value");
      }
      break;
    }
    case ValueSpecifierCase::VALUE_SPECIFIER_NOT_SET: {
      break;
    }
      PANIC_DUE_TO_CORRUPT_ENUM;
    }
  }
  return bucket_id;
}

/*Initially all Envoy's quota assignments are empty. The rate limit quota filter requests quota
   assignment from RLQS when the request matches a bucket for the first time. The behavior of the
   filter while it waits for the initial assignment is determined by the no_assignment_behavior
   value.
*/

void RateLimitQuotaFilter::setDecoderFilterCallbacks(
    Http::StreamDecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  // TODO(tyxia) Perform the CEL matching on
  // And can move it to initical_call
  // buildBuckets(headrs, *config_);
  if (rate_limit_client_->startStream() == true) {
    rate_limit_client_->rateLimit();
    return Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  }

  return Envoy::Http::FilterHeadersStatus::Continue;
}

void RateLimitQuotaFilter::onDestroy() { rate_limit_client_->closeStream(); }

// TODO(tyxia) CEL expression.
// C++ exception with description "Didn't find a registered implementation for 'test_action' with
// type URL: 'envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings'"
// thrown in the test body. Looks like I need to implement the action factory????
// https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/test/common/http/match_delegate/config_test.cc;rcl=453698037;l=211
// 1. use action to create the matcher tree
// 2. use test action in the test to trigger the action???
std::vector<BucketSettings> RateLimitQuotaFilter::buildMatcher() {
  // TODO(tyxia) action context RouteActionContext/FilterChainActionContext
  // https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/common/router/config_impl.h;rcl=466968687;l=1199
  std::cout << "matcher start_1" << std::endl;
  auto bucket_matcher = config_->bucket_matchers();
  // absl::string_view context = "";
  // Matcher::MatchTreeFactory<Http::HttpMatchingData, absl::string_view> factory(
  //     context, factory_context_.getServerFactoryContext(), visitor_);
  // //
  // https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/test/common/matcher/matcher_test.cc;rcl=468950517;l=80
  // TODO(tyxia) This caused the error above but maybe we don't even need this at all along with
  // match action etc matcher_ = factory.create(bucket_matcher)(); Http::HttpMatchingData
  // match_data; auto result = matcher_->match(match_data); result.on_match_->action_ab();

  // https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/test/common/router/route_fuzz_test.cc;rcl=469074906;l=50
  std::cout << "matcher start_2" << std::endl;
  auto field_matchers = bucket_matcher.matcher_list().matchers();
  std::vector<BucketSettings> settings;
  for (auto matcher : field_matchers) {
    auto action = matcher.on_match().action();
    if (action.typed_config().type_url().find(
            "envoy.extensions.filters.http.rate_limit_quota.v3.RateLimitQuotaBucketSettings") ==
        std::string::npos) {
      ASSERT("NOT FOUND");
    }

    envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings setting;
    MessageUtil::unpackTo(action.typed_config(), setting);
    settings.push_back(setting);
  }

  return settings;
}

/////////////////FINAL VERSION//////////////////////////
////////////////////////////////////////////////////////
// Contextual information used to construct the onMatch actions for a match tree.
struct RateLimitOnMactchActionContext {
  // RateLimitQuotaValidationVisitor visitor;
  // Server::Configuration::FactoryContext& factory_context;
  // Http::Matching::HttpMatchingDataImpl data;
  // Network::ConnectionInfoProvider& provider;
};

// TODO(tyxia) Orangize the file put them into headers maybe
// This class implements the on_match action behavior.
class RateLimitOnMactchAction : public Matcher::ActionBase<BucketId>,
                                public Logger::Loggable<Logger::Id::filter> {
public:
  // TODO(tyxia) Efficient coding pass by ref, capture etc.
  explicit RateLimitOnMactchAction(RateLimitQuotaBucketSettings settings)
      : setting_(std::move(settings)) {}

  BucketId generateBucketId(const Http::Matching::HttpMatchingDataImpl& data,
                            Server::Configuration::FactoryContext& factory_context,
                            RateLimitQuotaValidationVisitor& visitor) const {
    BucketId bucket_id;
    std::unique_ptr<Matcher::MatchInputFactory<Http::HttpMatchingData>> input_factory_ptr = nullptr;
    for (auto id_builder : setting_.bucket_id_builder().bucket_id_builder()) {
      std::string bucket_id_key = id_builder.first;
      auto builder_method = id_builder.second;

      // Generate the bucket id based on builder method type.
      switch (builder_method.value_specifier_case()) {
      // Retrieve the static string value directly from the config.
      case ValueSpecifierCase::kStringValue: {
        // Build the final bucket id directly from config.
        bucket_id.mutable_bucket()->insert({bucket_id_key, builder_method.string_value()});
        break;
      }
      // Retrieve the dynamic value from the `custom_value` typed extension config.
      case ValueSpecifierCase::kCustomValue: {
        // Initialize the pointer to input factory on first use.
        if (input_factory_ptr == nullptr) {
          input_factory_ptr = std::make_unique<Matcher::MatchInputFactory<Http::HttpMatchingData>>(
              factory_context.messageValidationVisitor(), visitor);
        }
        // Create `DataInput` factory callback from the config.
        Matcher::DataInputFactoryCb<Http::HttpMatchingData> data_input_cb =
            input_factory_ptr->createDataInput(builder_method.custom_value());
        auto result = data_input_cb()->get(data);
        // If result has data.
        if (result.data_) {
          if (!result.data_.value().empty()) {
            // Build the bucket id from the matched result.
            bucket_id.mutable_bucket()->insert({bucket_id_key, result.data_.value()});
          } else {
            ENVOY_LOG(debug, "Empty matched result.");
          }
        } else {
          ENVOY_LOG(debug, "Failed to retrive the result from custom value");
        }
        break;
      }
      case ValueSpecifierCase::VALUE_SPECIFIER_NOT_SET: {
        break;
      }
        PANIC_DUE_TO_CORRUPT_ENUM;
      }
    }
    return bucket_id;
  }

private:
  RateLimitQuotaBucketSettings setting_;
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
        MessageUtil::downcastAndValidate<const RateLimitQuotaBucketSettings&>(config,
                                                                              validation_visitor);
    return [bucket_settings = std::move(bucket_settings)]() {
      return std::make_unique<RateLimitOnMactchAction>(std::move(bucket_settings));
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<RateLimitQuotaBucketSettings>();
  }
};

REGISTER_FACTORY(RateLimitOnMactchActionFactory,
                 Matcher::ActionFactory<RateLimitOnMactchActionContext>);

void RateLimitQuotaFilter::createMatcherTree() {
  std::cout << "Create matcher!!!" << std::endl;
  RateLimitOnMactchActionContext context;
  Matcher::MatchTreeFactory<Http::HttpMatchingData, RateLimitOnMactchActionContext> factory(
      context, factory_context_.getServerFactoryContext(), visitor_);
  matcher_ = factory.create(config_->bucket_matchers())();
}

// Example route matcher
// https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/common/router/config_impl.cc;rcl=469339328;l=1776
// https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/test/common/matcher/matcher_test.cc;rcl=468950517;l=80
// TODO(tyxia) This caused the error above but maybe we don't even need this at all along with match
// action etc
BucketId RateLimitQuotaFilter::requestMatching(const Http::RequestHeaderMap& headers) {
  // TODO(tyxia) action context RouteActionContext/FilterChainActionContext
  // https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/source/common/router/config_impl.h;rcl=466968687;l=1199
  BucketId id;
  std::cout << "Perform request matching!!!" << std::endl;
  // Initialize the data pointer on first use and reuse it for subsequent requests.
  if (data_ptr_ == nullptr) {
    if (callbacks_ != nullptr) {
      data_ptr_ = std::make_unique<Http::Matching::HttpMatchingDataImpl>(
          callbacks_->streamInfo().downstreamAddressProvider());
    } else {
      ENVOY_LOG(error, "Filter callback has not been initialized yet.");
    }
  }

  // TODO(tyxia) avoid create the data object on every request which is very expensive!!!
  // Thread safe?? Test multiple requests as well.
  // Http::Matching::HttpMatchingDataImpl
  // data(callbacks_->streamInfo().downstreamAddressProvider()); data.onRequestHeaders(headers);
  // auto match = Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, data);
  ASSERT(data_ptr_ != nullptr);
  ASSERT(matcher_ != nullptr);
  if (data_ptr_ == nullptr || matcher_ == nullptr) {
    if (data_ptr_ == nullptr) {
      ENVOY_LOG(error, "Matching data object has not been initialized yet.");
    }

    if (matcher_ == nullptr) {
      ENVOY_LOG(error, "Matcher has not been initialized yet.");
    }
  } else {
    data_ptr_->onRequestHeaders(headers);
    auto match = Matcher::evaluateMatch<Http::HttpMatchingData>(*matcher_, *data_ptr_);
    if (match.result_) {
      std::cout << "Matcher succeed!!!" << std::endl;
      const auto result = match.result_();
      // TODO(tyxia) Remove ASSERTS???? change to error or logging
      ASSERT(result->typeUrl() == RateLimitOnMactchAction::staticTypeUrl());
      ASSERT(dynamic_cast<RateLimitOnMactchAction*>(result.get()));
      const RateLimitOnMactchAction& match_action =
          static_cast<const RateLimitOnMactchAction&>(*result);
      id = match_action.generateBucketId(*data_ptr_, factory_context_, visitor_);
    } else {
      ENVOY_LOG(debug, "Failed to match the request.");
    }
  }
  return id;
}

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
