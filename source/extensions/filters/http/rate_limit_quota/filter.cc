#include "source/extensions/filters/http/rate_limit_quota/filter.h"
#include <memory>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using ValueSpecifierCase = ::envoy::extensions::filters::http::rate_limit_quota::v3::
    RateLimitQuotaBucketSettings_BucketIdBuilder_ValueBuilder::ValueSpecifierCase;

// TODO(tyxia) May not be needed if I don't build the matcher tree on my own.
class OnMactchAction
    : public Matcher::ActionBase<
          envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings> {};

class OnMactchActionFactory
    : public Matcher::ActionFactory<Envoy::Http::Matching::HttpFilterActionContext> {
public:
  std::string name() const override { return "rate_limit_quota"; }
  Matcher::ActionFactoryCb createActionFactoryCb(const Protobuf::Message&,
                                                 Envoy::Http::Matching::HttpFilterActionContext&,
                                                 ProtobufMessage::ValidationVisitor&) override {
    return []() { return std::make_unique<OnMactchAction>(); };
  }
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<BucketSettings>();
  }
};

REGISTER_FACTORY(OnMactchActionFactory,
                 Matcher::ActionFactory<Envoy::Http::Matching::HttpFilterActionContext>);

// TODO(tyxia) Think about moving this to indepenet file. No need for a new class at this moment.
// TODO(tyxia) buildBuckets might be the part of filter rather than the client.
// * the data plane sends a usage report for requests matched into the bucket with ``BucketId``
//   to the control plane
// * the control plane sends an assignment for the bucket with ``BucketId`` to the data plane
//   Bucket ID.
// https://github.com/envoyproxy/envoy/blob/9dfd549158665d403c7de9e0079a0759be44ed74/api/envoy/extensions/filters/http/rate_limit_quota/v3/rate_limit_quota.proto#L64
// TODO(tyxia) bucksettings should be resovled from the on_match action.
BucketId RateLimitQuotaFilter::buildBucketsId(
    const Http::RequestHeaderMap& headers,
    const envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings&
        settings) {
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

// An action that evaluates to a proto StringValue.
// TODO(tyxia) maybe replace the string_view below with this action context.
struct StringAction : public Matcher::ActionBase<ProtobufWkt::StringValue> {
  explicit StringAction(const std::string& string) : string_(string) {}

  const std::string string_;

  bool operator==(const StringAction& other) const { return string_ == other.string_; }
};


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
  // // https://source.corp.google.com/piper///depot/google3/third_party/envoy/src/test/common/matcher/matcher_test.cc;rcl=468950517;l=80
  // TODO(tyxia) This caused the error above but maybe we don't even need this at all along with match action etc
  // matcher_ = factory.create(bucket_matcher)();
  // Http::HttpMatchingData match_data;
  // auto result = matcher_->match(match_data);
  // result.on_match_->action_ab();

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

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
