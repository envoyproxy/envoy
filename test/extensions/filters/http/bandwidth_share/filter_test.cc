#include "envoy/config/core/v3/base.pb.h"
#include "envoy/matcher/matcher.h"

#include "source/common/matcher/actions/string_returning_action.h"
#include "source/extensions/filters/http/bandwidth_share/filter.h"
#include "source/extensions/filters/http/bandwidth_share/filter_config.h"

#include "test/mocks/http/mocks.h"
#include "test/mocks/server/server_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using Envoy::Http::HttpMatchingData;

using testing::AnyNumber;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

class MockStringReturningAction : public Matcher::Actions::StringReturningAction {
public:
  MOCK_METHOD(std::string, getOutputString, (const StreamInfo::StreamInfo&), (const));
  MOCK_METHOD(absl::string_view, typeUrl, (), (const));
};

class MockMatchTree : public Matcher::MatchTree<HttpMatchingData> {
public:
  void setReturnedString(absl::string_view str) {
    auto ret = std::make_shared<MockStringReturningAction>();
    EXPECT_CALL(*ret, getOutputString).Times(AnyNumber()).WillRepeatedly(Return(std::string(str)));
    EXPECT_CALL(*this, match).WillRepeatedly(Return(Matcher::ActionMatchResult(ret)));
  }
  MOCK_METHOD(Matcher::ActionMatchResult, match,
              (const HttpMatchingData& matching_data, Matcher::SkippedMatchCb skipped_match_cb));
};

class FilterTest : public Event::TestUsingSimulatedTime, public testing::Test {
public:
  FilterTest() = default;

  void TearDown() override { filter_->onDestroy(); }

  void initFilter() {
    // Unify the dispatcher so that it's correctly connected to fake time.
    ON_CALL(encoder_callbacks_, dispatcher).WillByDefault(Return(decoder_callbacks_.dispatcher_));
    ON_CALL(decoder_callbacks_.dispatcher_, 
    auto tenant_name_selector = std::make_unique<MockMatchTree>();
    mock_tenant_name_selector_ = tenant_name_selector.get();
    config_ = std::make_shared<FilterConfig>(
        server_factory_context_, bucket_singleton_, request_bucket_id_, response_bucket_id_,
        response_trailer_prefix_, std::move(tenant_name_selector), std::move(tenant_configs_),
        default_tenant_config_);
    filter_ = std::make_shared<BandwidthShare>(config_);
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    EXPECT_CALL(decoder_callbacks_, bufferLimit()).Times(AnyNumber()).WillRepeatedly(Return(2048));
  }

  uint64_t counterValue(const std::string& name) {
    stats_.forEachCounter(
        [](std::size_t) {},
        [](Stats::Counter& counter) { std::cerr << counter.name() << std::endl; });
    const auto counter = TestUtility::findCounter(stats_, name);
    return counter != nullptr ? counter->value() : 0;
  }
  uint64_t gaugeValue(const std::string& name) {
    const auto gauge = TestUtility::findGauge(stats_, name);
    return gauge != nullptr ? gauge->value() : 0;
  }

  void prepareBucket(absl::string_view name, uint32_t kbps,
                     std::chrono::milliseconds fill_interval) {
    envoy::config::core::v3::RuntimeUInt32 runtime_proto_value;
    runtime_proto_value.set_runtime_key(absl::StrCat("runtime_key_", runtime_key_index_++));
    runtime_proto_value.set_default_value(kbps);
    bucket_singleton_
        ->setBucket(name, Runtime::UInt32(runtime_proto_value, server_factory_context_.runtime()),
                    fill_interval)
        .IgnoreError();
  }

  uint32_t runtime_key_index_ = 0;
  Stats::IsolatedStoreImpl stats_;
  NiceMock<Server::Configuration::MockServerFactoryContext> server_factory_context_;
  std::shared_ptr<TokenBucketSingleton> bucket_singleton_ =
      TokenBucketSingleton::get(server_factory_context_.singletonManager(),
                                server_factory_context_.timeSource(), *stats_.rootScope());
  absl::optional<absl::string_view> request_bucket_id_;
  absl::optional<absl::string_view> response_bucket_id_;
  absl::optional<absl::string_view> response_trailer_prefix_;
  absl::flat_hash_map<std::string, FilterConfig::TenantConfig> tenant_configs_;
  FilterConfig::TenantConfig default_tenant_config_{1, false};
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  NiceMock<Runtime::MockLoader> runtime_;
  MockMatchTree* mock_tenant_name_selector_ = nullptr;
  std::shared_ptr<FilterConfig> config_;
  std::shared_ptr<BandwidthShare> filter_;
  Http::TestRequestHeaderMapImpl request_headers_;
  Http::TestRequestTrailerMapImpl request_trailers_;
  Http::TestResponseHeaderMapImpl response_headers_;
  Http::TestResponseTrailerMapImpl response_trailers_;
  Buffer::OwnedImpl data_;
  const Http::TestResponseTrailerMapImpl empty_trailers_;
  template <class D> void advanceTime(const D& duration) {
    simTime().advanceTimeAndRun(duration, decoder_callbacks_.dispatcher(),
                                Event::Dispatcher::RunType::Block);
  }
  void triggerZeroTimer() { advanceTime(std::chrono::seconds(0)); }
  // TODO:remove once tests are good
  LogLevelSetter log_level_setter_ = LogLevelSetter(spdlog::level::debug);
};

TEST_F(FilterTest, DisabledJustPassesThrough) {
  // Unset request_bucket_id_ and response_bucket_id_ means disabled.
  initFilter();
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data_, false));
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
  EXPECT_THAT(counterValue("bandwidth_share.bytes"), 0U);
  EXPECT_THAT(response_trailers_, HeaderMapEqualRef(&empty_trailers_));
}

TEST_F(FilterTest, LimitOnDecode) {
  request_bucket_id_ = "request_bucket";
  prepareBucket("request_bucket", 1, std::chrono::milliseconds{20});
  initFilter();
  mock_tenant_name_selector_->setReturnedString("tenant");
  // Send a small amount of data which should be within limit.
  Buffer::OwnedImpl data1("hello");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_CALL(decoder_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual("hello"), false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data1, false));
  triggerZeroTimer();
  EXPECT_THAT(counterValue("bandwidth_share.bytes"), 5U);

  // Advance time by 1s which should refill all tokens.
  advanceTime(std::chrono::seconds(1));

  // Receive 1024+512 bytes of data which is 1.5s of data.
  EXPECT_CALL(decoder_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark());
  Buffer::OwnedImpl data2(std::string(1024 + 512, 'a'));
  EXPECT_CALL(decoder_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(1024, 'a')), false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data2, false));
  EXPECT_THAT(counterValue("bandwidth_share.bytes"), 1029U);
  EXPECT_THAT(gaugeValue("bandwidth_share.streams_currently_limited"), 1U);
  EXPECT_THAT(gaugeValue("bandwidth_share.bytes_pending"), 512U);
  EXPECT_THAT(gaugeValue("bandwidth-share.bytes"), 0U);
  EXPECT_CALL(decoder_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark());

  EXPECT_CALL(decoder_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(512, 'a')), false));
  // Advance time by 1s which should refill all tokens.
  advanceTime(std::chrono::seconds(1));

  EXPECT_THAT(counterValue("bandwidth_share.bytes"), 1029U);
  EXPECT_THAT(gaugeValue("bandwidth_share.streams_currently_limited"), 1U);
  EXPECT_THAT(gaugeValue("bandwidth_share.bytes_pending"), 512U);
  EXPECT_THAT(gaugeValue("bandwidth-share.bytes"), 512U);

  // Add another 10 bytes and end stream.
  Buffer::OwnedImpl data3(std::string(10, 'a'));
  EXPECT_CALL(decoder_callbacks_,
              injectDecodedDataToFilterChain(BufferStringEqual(std::string(1024, 'a')), true));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data3, true));
}

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
