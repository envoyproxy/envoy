#include "envoy/extensions/filters/http/sampling_decision/v3/sampling_decision.pb.h"

#include "source/extensions/filters/http/sampling_decision/sampling_decision_filter.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SamplingDecision {
namespace {

class SamplingDecisionFilterTest : public testing::Test {
public:
  SamplingDecisionFilterTest() {
    ON_CALL(runtime_.snapshot_, featureEnabled(_, _, _, _)).WillByDefault(Return(true));
    ON_CALL(runtime_.snapshot_, getInteger(_, _)).WillByDefault(Return(5));

    // Set up dynamic metadata to actually store values when setDynamicMetadata is called.
    ON_CALL(decoder_callbacks_.stream_info_, setDynamicMetadata(_, _))
        .WillByDefault(
            testing::Invoke([this](const std::string& ns, const Protobuf::Struct& value) {
              (*metadata_.mutable_filter_metadata())[ns] = value;
            }));
    ON_CALL(decoder_callbacks_.stream_info_, dynamicMetadata()).WillByDefault(ReturnRef(metadata_));
    ON_CALL(testing::Const(decoder_callbacks_.stream_info_), dynamicMetadata())
        .WillByDefault(ReturnRef(metadata_));
  }

  void setupFilter(const std::string& yaml) {
    envoy::extensions::filters::http::sampling_decision::v3::SamplingDecision proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    config_ = std::make_shared<SamplingDecisionConfig>(proto_config);
    filter_ = std::make_shared<SamplingDecisionFilter>(config_, runtime_, random_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
  }

  NiceMock<Runtime::MockLoader> runtime_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  envoy::config::core::v3::Metadata metadata_;
  std::shared_ptr<SamplingDecisionConfig> config_;
  std::shared_ptr<SamplingDecisionFilter> filter_;
  Http::TestRequestHeaderMapImpl request_headers_;
};

TEST_F(SamplingDecisionFilterTest, BasicSamplingEnabled) {
  const std::string yaml = R"EOF(
runtime_key: "sampling.test"
percent_sampled:
  numerator: 50
  denominator: HUNDRED
)EOF";

  setupFilter(yaml);

  EXPECT_CALL(random_, random()).WillOnce(Return(25));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("sampling.test", 50, 25, 100))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("sampling.test", 50)).WillOnce(Return(50));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  // Verify metadata was set.
  const auto& metadata = decoder_callbacks_.stream_info_.dynamicMetadata().filter_metadata();
  EXPECT_EQ(metadata.count("envoy.filters.http.sampling_decision"), 1);
  const auto& sampling_metadata = metadata.at("envoy.filters.http.sampling_decision").fields();
  EXPECT_EQ(sampling_metadata.at("sampled").bool_value(), true);
  EXPECT_EQ(sampling_metadata.at("runtime_key").string_value(), "sampling.test");
  EXPECT_EQ(sampling_metadata.at("numerator").number_value(), 50);
  EXPECT_EQ(sampling_metadata.at("denominator").number_value(), 100);
}

TEST_F(SamplingDecisionFilterTest, BasicSamplingDisabled) {
  const std::string yaml = R"EOF(
runtime_key: "sampling.test"
percent_sampled:
  numerator: 50
  denominator: HUNDRED
)EOF";

  setupFilter(yaml);

  EXPECT_CALL(random_, random()).WillOnce(Return(75));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("sampling.test", 50, 75, 100))
      .WillOnce(Return(false));
  EXPECT_CALL(runtime_.snapshot_, getInteger("sampling.test", 50)).WillOnce(Return(50));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  // Verify metadata was set with sampled=false.
  const auto& metadata = decoder_callbacks_.stream_info_.dynamicMetadata().filter_metadata();
  EXPECT_EQ(metadata.count("envoy.filters.http.sampling_decision"), 1);
  const auto& sampling_metadata = metadata.at("envoy.filters.http.sampling_decision").fields();
  EXPECT_EQ(sampling_metadata.at("sampled").bool_value(), false);
  EXPECT_EQ(sampling_metadata.at("runtime_key").string_value(), "sampling.test");
  EXPECT_EQ(sampling_metadata.at("numerator").number_value(), 50);
  EXPECT_EQ(sampling_metadata.at("denominator").number_value(), 100);
}

TEST_F(SamplingDecisionFilterTest, CustomMetadataNamespace) {
  const std::string yaml = R"EOF(
runtime_key: "sampling.test"
percent_sampled:
  numerator: 100
  denominator: HUNDRED
metadata_namespace: "custom.namespace"
)EOF";

  setupFilter(yaml);

  EXPECT_CALL(random_, random()).WillOnce(Return(0));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("sampling.test", 100, 0, 100))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("sampling.test", 100)).WillOnce(Return(100));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  // Verify metadata was set under custom namespace.
  const auto& metadata = decoder_callbacks_.stream_info_.dynamicMetadata().filter_metadata();
  EXPECT_EQ(metadata.count("custom.namespace"), 1);
  const auto& sampling_metadata = metadata.at("custom.namespace").fields();
  EXPECT_EQ(sampling_metadata.at("sampled").bool_value(), true);
  EXPECT_EQ(sampling_metadata.at("runtime_key").string_value(), "sampling.test");
  EXPECT_EQ(sampling_metadata.at("numerator").number_value(), 100);
  EXPECT_EQ(sampling_metadata.at("denominator").number_value(), 100);
}

TEST_F(SamplingDecisionFilterTest, RuntimeOverride) {
  const std::string yaml = R"EOF(
runtime_key: "sampling.test"
percent_sampled:
  numerator: 50
  denominator: HUNDRED
)EOF";

  setupFilter(yaml);

  // Runtime overrides the configured value to 75.
  EXPECT_CALL(random_, random()).WillOnce(Return(60));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("sampling.test", 50, 60, 100))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("sampling.test", 50)).WillOnce(Return(75));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  // Verify metadata contains the runtime-overridden value.
  const auto& metadata = decoder_callbacks_.stream_info_.dynamicMetadata().filter_metadata();
  const auto& sampling_metadata = metadata.at("envoy.filters.http.sampling_decision").fields();
  EXPECT_EQ(sampling_metadata.at("numerator").number_value(), 75);
}

TEST_F(SamplingDecisionFilterTest, UseIndependentRandomness) {
  const std::string yaml = R"EOF(
runtime_key: "sampling.test"
percent_sampled:
  numerator: 50
  denominator: HUNDRED
use_independent_randomness: true
)EOF";

  setupFilter(yaml);

  // When use_independent_randomness is true, we should always use random_.random().
  EXPECT_CALL(random_, random()).WillOnce(Return(25));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("sampling.test", 50, 25, 100))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("sampling.test", 50)).WillOnce(Return(50));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  const auto& metadata = decoder_callbacks_.stream_info_.dynamicMetadata().filter_metadata();
  const auto& sampling_metadata = metadata.at("envoy.filters.http.sampling_decision").fields();
  EXPECT_EQ(sampling_metadata.at("sampled").bool_value(), true);
}

TEST_F(SamplingDecisionFilterTest, DeterministicSamplingWithRequestId) {
  const std::string yaml = R"EOF(
runtime_key: "sampling.test"
percent_sampled:
  numerator: 50
  denominator: HUNDRED
use_independent_randomness: false
)EOF";

  setupFilter(yaml);

  // Set up a request ID provider.
  auto stream_id_provider = std::make_shared<StreamInfo::StreamIdProviderImpl>("test-request-id");
  EXPECT_CALL(decoder_callbacks_.stream_info_, getStreamIdProvider())
      .WillRepeatedly(Return(makeOptRef<const StreamInfo::StreamIdProvider>(*stream_id_provider)));

  // The random value should be derived from the request ID.
  // If toInteger() doesn't return a value, the filter will fall back to random_.random().
  auto to_integer_result = stream_id_provider->toInteger();
  if (to_integer_result.has_value()) {
    EXPECT_CALL(random_, random()).Times(0);
    const uint64_t expected_random_value = to_integer_result.value() % 100;
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("sampling.test", 50, expected_random_value, 100))
        .WillOnce(Return(true));
  } else {
    // Fall back to random generator if request ID can't be converted to integer.
    EXPECT_CALL(random_, random()).WillOnce(Return(25));
    EXPECT_CALL(runtime_.snapshot_, featureEnabled("sampling.test", 50, 25, 100))
        .WillOnce(Return(true));
  }
  EXPECT_CALL(runtime_.snapshot_, getInteger("sampling.test", 50)).WillOnce(Return(50));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  const auto& metadata = metadata_.filter_metadata();
  EXPECT_EQ(metadata.count("envoy.filters.http.sampling_decision"), 1);
}

TEST_F(SamplingDecisionFilterTest, TenThousandDenominator) {
  const std::string yaml = R"EOF(
runtime_key: "sampling.test"
percent_sampled:
  numerator: 5000
  denominator: TEN_THOUSAND
)EOF";

  setupFilter(yaml);

  EXPECT_CALL(random_, random()).WillOnce(Return(2500));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("sampling.test", 5000, 2500, 10000))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("sampling.test", 5000)).WillOnce(Return(5000));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  const auto& metadata = decoder_callbacks_.stream_info_.dynamicMetadata().filter_metadata();
  const auto& sampling_metadata = metadata.at("envoy.filters.http.sampling_decision").fields();
  EXPECT_EQ(sampling_metadata.at("denominator").number_value(), 10000);
}

TEST_F(SamplingDecisionFilterTest, MillionDenominator) {
  const std::string yaml = R"EOF(
runtime_key: "sampling.test"
percent_sampled:
  numerator: 500000
  denominator: MILLION
)EOF";

  setupFilter(yaml);

  EXPECT_CALL(random_, random()).WillOnce(Return(250000));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("sampling.test", 500000, 250000, 1000000))
      .WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger("sampling.test", 500000)).WillOnce(Return(500000));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  const auto& metadata = decoder_callbacks_.stream_info_.dynamicMetadata().filter_metadata();
  const auto& sampling_metadata = metadata.at("envoy.filters.http.sampling_decision").fields();
  EXPECT_EQ(sampling_metadata.at("denominator").number_value(), 1000000);
}

TEST_F(SamplingDecisionFilterTest, DefaultPercentSampled) {
  const std::string yaml = R"EOF(
runtime_key: "sampling.test"
)EOF";

  setupFilter(yaml);

  // With no percent_sampled configured, it defaults to 0/100.
  EXPECT_CALL(random_, random()).WillOnce(Return(0));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("sampling.test", 0, 0, 100))
      .WillOnce(Return(false));
  EXPECT_CALL(runtime_.snapshot_, getInteger("sampling.test", 0)).WillOnce(Return(0));

  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));

  const auto& metadata = decoder_callbacks_.stream_info_.dynamicMetadata().filter_metadata();
  const auto& sampling_metadata = metadata.at("envoy.filters.http.sampling_decision").fields();
  EXPECT_EQ(sampling_metadata.at("sampled").bool_value(), false);
  EXPECT_EQ(sampling_metadata.at("numerator").number_value(), 0);
  EXPECT_EQ(sampling_metadata.at("denominator").number_value(), 100);
}

TEST_F(SamplingDecisionFilterTest, AlwaysContinue) {
  const std::string yaml = R"EOF(
runtime_key: "sampling.test"
percent_sampled:
  numerator: 100
  denominator: HUNDRED
)EOF";

  setupFilter(yaml);

  EXPECT_CALL(random_, random()).WillOnce(Return(0));
  EXPECT_CALL(runtime_.snapshot_, featureEnabled(_, _, _, _)).WillOnce(Return(true));
  EXPECT_CALL(runtime_.snapshot_, getInteger(_, _)).WillOnce(Return(100));

  // Filter should always return Continue regardless of sampling decision.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
}

TEST_F(SamplingDecisionFilterTest, ConfigValidation) {
  // Test that config properly parses all fields.
  const std::string yaml = R"EOF(
runtime_key: "test.key"
percent_sampled:
  numerator: 25
  denominator: HUNDRED
use_independent_randomness: true
metadata_namespace: "my.namespace"
)EOF";

  envoy::extensions::filters::http::sampling_decision::v3::SamplingDecision proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  auto config = std::make_shared<SamplingDecisionConfig>(proto_config);

  EXPECT_EQ(config->runtimeKey(), "test.key");
  EXPECT_EQ(config->percentSampled().numerator(), 25);
  EXPECT_EQ(config->percentSampled().denominator(), envoy::type::v3::FractionalPercent::HUNDRED);
  EXPECT_EQ(config->useIndependentRandomness(), true);
  EXPECT_EQ(config->metadataNamespace(), "my.namespace");
}

TEST_F(SamplingDecisionFilterTest, DefaultMetadataNamespace) {
  const std::string yaml = R"EOF(
runtime_key: "sampling.test"
percent_sampled:
  numerator: 50
  denominator: HUNDRED
)EOF";

  envoy::extensions::filters::http::sampling_decision::v3::SamplingDecision proto_config;
  TestUtility::loadFromYaml(yaml, proto_config);
  auto config = std::make_shared<SamplingDecisionConfig>(proto_config);

  EXPECT_EQ(config->metadataNamespace(), "envoy.filters.http.sampling_decision");
}

} // namespace
} // namespace SamplingDecision
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
