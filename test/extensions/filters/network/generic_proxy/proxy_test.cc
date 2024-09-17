#include <memory>
#include <string>
#include <utility>

#include "source/common/tracing/tracer_manager_impl.h"
#include "source/extensions/filters/network/generic_proxy/proxy.h"

#include "test/extensions/filters/network/generic_proxy/fake_codec.h"
#include "test/extensions/filters/network/generic_proxy/mocks/codec.h"
#include "test/extensions/filters/network/generic_proxy/mocks/filter.h"
#include "test/extensions/filters/network/generic_proxy/mocks/route.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::ByMove;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace {

static const std::string DEFAULT_LOG_FORMAT =
    "%HOST% %PATH% %METHOD% %PROTOCOL% %REQUEST_PROPERTY(request-key)% "
    "%RESPONSE_PROPERTY(response-key)% "
    "%REQUEST_PROPERTY(non-exist-key)% %GENERIC_RESPONSE_CODE% %RESPONSE_CODE_DETAILS%";

class MockRouteConfigProvider : public Rds::RouteConfigProvider {
public:
  MockRouteConfigProvider() { ON_CALL(*this, config()).WillByDefault(Return(route_config_)); }

  MOCK_METHOD(Rds::ConfigConstSharedPtr, config, (), (const));
  MOCK_METHOD(const absl::optional<Rds::RouteConfigProvider::ConfigInfo>&, configInfo, (), (const));
  MOCK_METHOD(SystemTime, lastUpdated, (), (const));
  MOCK_METHOD(absl::Status, onConfigUpdate, ());

  std::shared_ptr<NiceMock<MockRouteMatcher>> route_config_{new NiceMock<MockRouteMatcher>()};
};

class FilterConfigTest : public testing::Test {
public:
  void initializeFilterConfig(bool with_tracing = false, AccessLogInstanceSharedPtr logger = {},
                              bool allow_no_decoder_filter = false) {
    if (with_tracing) {
      tracer_ = std::make_shared<NiceMock<Tracing::MockTracer>>();

      const std::string tracing_config_yaml = R"EOF(
      max_path_tag_length: 256
      spawn_upstream_span: true
      )EOF";

      Tracing::ConnectionManagerTracingConfigProto tracing_config;

      TestUtility::loadFromYaml(tracing_config_yaml, tracing_config);

      tracing_config_ = std::make_unique<Tracing::ConnectionManagerTracingConfigImpl>(
          envoy::config::core::v3::TrafficDirection::OUTBOUND, tracing_config);
    }

    std::vector<NamedFilterFactoryCb> factories;

    if (mock_decoder_filters_.empty() && mock_stream_filters_.empty() && !allow_no_decoder_filter) {
      // At least one decoder filter for generic proxy.
      mock_decoder_filters_.push_back(
          {"mock_default_decoder_filter", std::make_shared<NiceMock<MockDecoderFilter>>()});
    }

    for (const auto& filter : mock_stream_filters_) {
      factories.push_back({filter.first, [f = filter.second](FilterChainFactoryCallbacks& cb) {
                             cb.addFilter(f);
                           }});
    }

    for (const auto& filter : mock_decoder_filters_) {
      factories.push_back({filter.first, [f = filter.second](FilterChainFactoryCallbacks& cb) {
                             cb.addDecoderFilter(f);
                           }});
    }
    for (const auto& filter : mock_encoder_filters_) {
      factories.push_back({filter.first, [f = filter.second](FilterChainFactoryCallbacks& cb) {
                             cb.addEncoderFilter(f);
                           }});
    }

    auto codec_factory = std::make_unique<NiceMock<MockCodecFactory>>();
    codec_factory_ = codec_factory.get();

    mock_route_entry_ = std::make_shared<NiceMock<MockRouteEntry>>();

    std::vector<AccessLogInstanceSharedPtr> access_logs;
    if (logger) {
      access_logs.push_back(logger);
    }

    filter_config_ = std::make_shared<FilterConfigImpl>(
        "generic_proxy.test_prefix.", std::move(codec_factory), route_config_provider_, factories,
        tracer_, std::move(tracing_config_), std::move(access_logs), code_or_flags_,
        factory_context_);
  }

  AccessLogInstanceSharedPtr loggerFormFormat(const std::string& format = DEFAULT_LOG_FORMAT) {
    envoy::config::core::v3::SubstitutionFormatString sff_config;
    sff_config.mutable_text_format_source()->set_inline_string(format);
    auto formatter =
        *Envoy::Formatter::SubstitutionFormatStringUtils::fromProtoConfig<FormatterContext>(
            sff_config, factory_context_);

    return std::make_shared<FileAccessLog>(
        Filesystem::FilePathAndType{}, nullptr, std::move(formatter),
        factory_context_.server_factory_context_.accessLogManager());
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  CodeOrFlags code_or_flags_{factory_context_.server_factory_context_};

  std::shared_ptr<NiceMock<Tracing::MockTracer>> tracer_;
  Tracing::ConnectionManagerTracingConfigPtr tracing_config_;

  std::shared_ptr<FilterConfig> filter_config_;

  std::shared_ptr<NiceMock<MockRouteConfigProvider>> route_config_provider_{
      new NiceMock<MockRouteConfigProvider>()};
  NiceMock<MockRouteMatcher>* route_matcher_ = route_config_provider_->route_config_.get();

  NiceMock<MockCodecFactory>* codec_factory_;

  using MockStreamFilterSharedPtr = std::shared_ptr<NiceMock<MockStreamFilter>>;
  using MockDecoderFilterSharedPtr = std::shared_ptr<NiceMock<MockDecoderFilter>>;
  using MockEncoderFilterSharedPtr = std::shared_ptr<NiceMock<MockEncoderFilter>>;

  std::vector<std::pair<std::string, MockStreamFilterSharedPtr>> mock_stream_filters_;
  std::vector<std::pair<std::string, MockDecoderFilterSharedPtr>> mock_decoder_filters_;
  std::vector<std::pair<std::string, MockEncoderFilterSharedPtr>> mock_encoder_filters_;

  std::shared_ptr<NiceMock<MockRouteEntry>> mock_route_entry_;
};

/**
 * Test the method that to get route entry.
 */
TEST_F(FilterConfigTest, RouteEntry) {
  initializeFilterConfig();

  EXPECT_CALL(*route_matcher_, routeEntry(_)).WillOnce(Return(mock_route_entry_));

  FakeStreamCodecFactory::FakeRequest fake_request;
  NiceMock<StreamInfo::MockStreamInfo> stream_info;
  const MatchInput match_input(fake_request, stream_info, MatchAction::RouteAction);

  EXPECT_EQ(filter_config_->routeEntry(match_input).get(), mock_route_entry_.get());
}

/**
 * Test creating an L7 filter chain.
 */
TEST_F(FilterConfigTest, CreateFilterChain) {
  auto mock_stream_filter = std::make_shared<NiceMock<MockStreamFilter>>();
  mock_stream_filters_ = {{"mock_0", mock_stream_filter},
                          {"mock_1", mock_stream_filter},
                          {"mock_2", mock_stream_filter}};

  initializeFilterConfig();

  NiceMock<MockFilterChainManager> cb;

  EXPECT_CALL(cb.callbacks_, addFilter(_))
      .Times(3)
      .WillRepeatedly(Invoke([&](StreamFilterSharedPtr filter) {
        EXPECT_EQ(filter.get(), mock_stream_filter.get());
      }));

  filter_config_->createFilterChain(cb);
}

/**
 * Simple test for getting codec factory.
 */
TEST_F(FilterConfigTest, CodecFactory) {
  initializeFilterConfig();

  EXPECT_EQ(&(filter_config_->codecFactory()), codec_factory_);
}

class FilterTest : public FilterConfigTest {
public:
  void initializeFilter(bool with_tracing = false, AccessLogInstanceSharedPtr logger = {},
                        bool allow_no_decoder_filter = false) {
    FilterConfigTest::initializeFilterConfig(with_tracing, logger, allow_no_decoder_filter);

    auto server_codec = std::make_unique<NiceMock<MockServerCodec>>();
    server_codec_ = server_codec.get();
    EXPECT_CALL(*codec_factory_, createServerCodec())
        .WillOnce(Return(ByMove(std::move(server_codec))));

    EXPECT_CALL(*server_codec_, setCodecCallbacks(_))
        .WillOnce(Invoke(
            [this](ServerCodecCallbacks& callback) { server_codec_callbacks_ = &callback; }));

    ON_CALL(*server_codec_, respond(_, _, _))
        .WillByDefault(
            Invoke([](Status status, absl::string_view data, const RequestHeaderFrame& req) {
              FakeStreamCodecFactory::FakeServerCodec codec;
              return codec.respond(status, data, req);
            }));

    filter_ = std::make_shared<Filter>(filter_config_, factory_context_);

    EXPECT_EQ(filter_.get(), server_codec_callbacks_);

    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  std::shared_ptr<Filter> filter_;

  ServerCodecCallbacks* server_codec_callbacks_{};

  NiceMock<MockServerCodec>* server_codec_{};

  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
};

TEST_F(FilterTest, SimpleOnNewConnection) {
  initializeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

TEST_F(FilterTest, SimpleOnData) {
  initializeFilter();

  Buffer::OwnedImpl fake_empty_buffer;

  EXPECT_CALL(*server_codec_, decode(_, _));
  filter_->onData(fake_empty_buffer, false);
}

TEST_F(FilterTest, OnDecodingFailureWithoutActiveStreams) {
  initializeFilter();

  Buffer::OwnedImpl fake_empty_buffer;

  EXPECT_CALL(*server_codec_, decode(_, _));
  filter_->onData(fake_empty_buffer, false);

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  server_codec_callbacks_->onDecodingFailure();

  EXPECT_EQ(filter_config_->stats().downstream_rq_decoding_error_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 0);
  // No request was received and no request was reset.
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 0);
}

TEST_F(FilterTest, OnDecodingSuccessWithNormalRequest) {
  auto mock_stream_filter = std::make_shared<NiceMock<MockStreamFilter>>();
  mock_stream_filters_ = {{"mock_0", mock_stream_filter},
                          {"mock_1", mock_stream_filter},
                          {"mock_2", mock_stream_filter}};

  initializeFilter();

  Buffer::OwnedImpl fake_empty_buffer;

  EXPECT_CALL(*server_codec_, decode(_, _));
  filter_->onData(fake_empty_buffer, false);

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  // Three mock factories was added.
  EXPECT_CALL(*mock_stream_filter, decodeHeaderFrame(_)).Times(3);

  server_codec_callbacks_->onDecodingSuccess(std::move(request));

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);

  EXPECT_EQ(1, filter_->activeStreamsForTest().size());
}

TEST_F(FilterTest, OnConnectedEvent) {
  initializeFilter();
  // Do nothing.
  filter_->onEvent(Network::ConnectionEvent::Connected);
}

TEST_F(FilterTest, OnConnectionClosedEvent) {
  initializeFilter();

  filter_->onEvent(Network::ConnectionEvent::RemoteClose);

  Buffer::OwnedImpl fake_empty_buffer;

  // Return directly if connection is closed.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(fake_empty_buffer, false));
}

TEST_F(FilterTest, GetConnection) {
  initializeFilter();

  EXPECT_EQ(&(filter_callbacks_.connection_), &filter_->downstreamConnection());
  // Return valid connection if connection is not closed.
  EXPECT_EQ(&(filter_callbacks_.connection_), filter_->connection().ptr());

  // Mock connection close.
  filter_callbacks_.connection_.close(Network::ConnectionCloseType::NoFlush);

  EXPECT_EQ(&(filter_callbacks_.connection_), &filter_->downstreamConnection());
  // Return nullopt if connection is closed.
  EXPECT_EQ(nullptr, filter_->connection().ptr());
}

TEST_F(FilterTest, BufferWaterMarkTest) {
  initializeFilter();
  filter_->onAboveWriteBufferHighWatermark();
  filter_->onBelowWriteBufferLowWatermark();
}

TEST_F(FilterTest, NullHeaderFrame) {
  initializeFilter();

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  filter_->onDecodingSuccess(nullptr, {});

  // Calling again will not cause any issue.
  filter_->onDecodingSuccess(nullptr, {});
}

TEST_F(FilterTest, NullCommonFrame) {
  initializeFilter();

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  filter_->onDecodingSuccess(RequestCommonFramePtr(nullptr));

  // Calling again will not cause any issue.
  filter_->onDecodingSuccess(RequestCommonFramePtr(nullptr));
}

TEST_F(FilterTest, RepeatedStreamIdForTwoStreams) {
  initializeFilter();

  auto request_0 = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  request_0->stream_frame_flags_ = {0, FrameFlags::FLAG_EMPTY};
  auto request_1 = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  request_1->stream_frame_flags_ = {0, FrameFlags::FLAG_EMPTY};

  filter_->onDecodingSuccess(std::move(request_0));

  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  filter_->onDecodingSuccess(std::move(request_1));

  EXPECT_EQ(0, filter_->activeStreamsForTest().size());
}

TEST_F(FilterTest, CommonFrameBeforeHeaderFrame) {
  initializeFilter();

  auto request_frame = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  filter_->onDecodingSuccess(std::move(request_frame));
}

TEST_F(FilterTest, NewStreamAndDispatcher) {
  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);

  EXPECT_EQ("mock_default_decoder_filter",
            mock_decoder_filters_[0].second->decoder_callbacks_->filterConfigName());
  mock_decoder_filters_[0].second->decoder_callbacks_->dispatcher();
}

TEST_F(FilterTest, NewStreamWithStartTime) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  mock_stream_filters_.push_back({"mock_0", mock_stream_filter_0});

  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  StartTime start_time;
  start_time.start_time =
      std::chrono::time_point<std::chrono::system_clock>(std::chrono::milliseconds(111111111));
  start_time.start_time_monotonic =
      std::chrono::time_point<std::chrono::steady_clock>(std::chrono::milliseconds(222222222));
  filter_->onDecodingSuccess(std::move(request), std::move(start_time));

  EXPECT_EQ(
      111111111LL,
      std::chrono::duration_cast<std::chrono::milliseconds>(
          mock_stream_filter_0->decoder_callbacks_->streamInfo().startTime().time_since_epoch())
          .count());
  EXPECT_EQ(222222222LL, std::chrono::duration_cast<std::chrono::milliseconds>(
                             mock_stream_filter_0->decoder_callbacks_->streamInfo()
                                 .startTimeMonotonic()
                                 .time_since_epoch())
                             .count());
}

TEST_F(FilterTest, OnDecodingFailureWithActiveStreams) {
  initializeFilter();

  auto request_0 = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  filter_->onDecodingSuccess(std::move(request_0));

  auto request_1 = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  filter_->onDecodingSuccess(std::move(request_1));

  EXPECT_EQ(2, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 2);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 2);

  Buffer::OwnedImpl fake_empty_buffer;
  EXPECT_CALL(*server_codec_, decode(_, _));
  filter_->onData(fake_empty_buffer, false);

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  server_codec_callbacks_->onDecodingFailure();

  EXPECT_EQ(0, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 2);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 2);
  EXPECT_EQ(
      factory_context_.store_.counter("generic_proxy.test_prefix.downstream_rq_flag.DPE").value(),
      2);
}

TEST_F(FilterTest, OnEncodingFailureWithActiveStreams) {
  initializeFilter();

  Buffer::OwnedImpl fake_empty_buffer;
  EXPECT_CALL(*server_codec_, decode(_, _)).WillOnce(Invoke([&](Buffer::Instance&, bool) {
    auto request_0 = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
    auto request_1 = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

    filter_->onDecodingSuccess(std::move(request_0));
    filter_->onDecodingSuccess(std::move(request_1));
  }));
  filter_->onData(fake_empty_buffer, false);

  EXPECT_EQ(2, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 2);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 2);

  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Return(EncodingResult{absl::InvalidArgumentError("encoding-error")}));
  auto response_0 = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  mock_decoder_filters_[0].second->decoder_callbacks_->onResponseHeaderFrame(std::move(response_0));

  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 2);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 1);
  EXPECT_EQ(
      factory_context_.store_.counter("generic_proxy.test_prefix.downstream_rq_flag.DPE").value(),
      1);
}

TEST_F(FilterTest, ActiveStreamRouteEntry) {
  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);

  auto active_stream = filter_->activeStreamsForTest().begin()->get();
  EXPECT_EQ(active_stream->routeEntry().ptr(), route_matcher_->route_entry_.get());
}

TEST_F(FilterTest, ActiveStreamPerFilterConfig) {
  mock_stream_filters_.push_back(
      {"fake_test_filter_name_0", std::make_shared<NiceMock<MockStreamFilter>>()});

  initializeFilter();

  EXPECT_CALL(*route_matcher_, routeEntry(_)).WillOnce(Return(mock_route_entry_));

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_CALL(*mock_route_entry_, perFilterConfig("fake_test_filter_name_0"))
      .WillOnce(Return(nullptr));
  EXPECT_EQ(nullptr, mock_stream_filters_[0].second->decoder_callbacks_->perFilterConfig());
}

TEST_F(FilterTest, ActiveStreamPerFilterConfigNoRouteEntry) {
  mock_stream_filters_.push_back(
      {"fake_test_filter_name_0", std::make_shared<NiceMock<MockStreamFilter>>()});

  initializeFilter();

  EXPECT_CALL(*route_matcher_, routeEntry(_)).WillOnce(Return(nullptr));

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(nullptr, mock_stream_filters_[0].second->decoder_callbacks_->perFilterConfig());
}

TEST_F(FilterTest, ActiveStreamConnection) {
  mock_stream_filters_.push_back(
      {"fake_test_filter_name_0", std::make_shared<NiceMock<MockStreamFilter>>()});

  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(&filter_callbacks_.connection_,
            mock_stream_filters_[0].second->decoder_callbacks_->connection());
}

TEST_F(FilterTest, ActiveStreamNoDecoderFilter) {
  initializeFilter(false, loggerFormFormat(), true);

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(0, filter_->activeStreamsForTest().size());

  // The stream will be reset directly.
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 1);
}

TEST_F(FilterTest, ActiveStreamAddFilters) {
  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_EQ(1, active_stream->decoderFiltersForTest().size());
  EXPECT_EQ(0, active_stream->encoderFiltersForTest().size());

  ActiveStream::FilterChainFactoryCallbacksHelper helper(*active_stream, {"fake_test"});

  auto new_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto new_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto new_filter_3 = std::make_shared<NiceMock<MockStreamFilter>>();

  helper.addDecoderFilter(new_filter_0);
  helper.addEncoderFilter(new_filter_0);

  helper.addDecoderFilter(new_filter_1);
  helper.addFilter(new_filter_3);

  EXPECT_EQ(4, active_stream->decoderFiltersForTest().size());
  EXPECT_EQ(2, active_stream->encoderFiltersForTest().size());

  EXPECT_EQ("mock_default_decoder_filter",
            active_stream->decoderFiltersForTest()[0]->context_.config_name);
  EXPECT_EQ("fake_test", active_stream->encoderFiltersForTest()[0]->context_.config_name);
}

TEST_F(FilterTest, ActiveStreamAddFiltersOrder) {
  auto filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto filter_2 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"fake_test_filter_name_0", filter_0},
                          {"fake_test_filter_name_1", filter_1},
                          {"fake_test_filter_name_2", filter_2}};

  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_EQ(3, active_stream->decoderFiltersForTest().size());
  EXPECT_EQ(3, active_stream->encoderFiltersForTest().size());

  EXPECT_EQ(filter_2.get(), active_stream->encoderFiltersForTest()[0]->filter_.get());
  EXPECT_EQ(filter_1.get(), active_stream->encoderFiltersForTest()[1]->filter_.get());
  EXPECT_EQ(filter_0.get(), active_stream->encoderFiltersForTest()[2]->filter_.get());
}

TEST_F(FilterTest, ActiveStreamSingleFrameFiltersContinueDecoding) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_2 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0},
                          {"mock_1", mock_stream_filter_1},
                          {"mock_2", mock_stream_filter_2}};

  initializeFilter();

  EXPECT_CALL(*mock_stream_filter_0, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  filter_->onDecodingSuccess(std::move(request));
  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  // Decoding is stopped when `decodeHeaderFrame` of `mock_stream_filter_0` is called.
  // Next filter is `mock_1`.
  EXPECT_EQ("mock_1", (*active_stream->nextDecoderHeaderFilterForTest())->filterConfigName());

  EXPECT_CALL(*mock_stream_filter_1, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  mock_stream_filter_0->decoder_callbacks_->continueDecoding();

  // Decoding is stopped when `decodeHeaderFrame` of `mock_stream_filter_1` is called.
  // Next filter is `mock_2`.
  EXPECT_EQ("mock_2", (*active_stream->nextDecoderHeaderFilterForTest())->filterConfigName());

  mock_stream_filter_1->decoder_callbacks_->continueDecoding();

  // Filter chain is completed.
  EXPECT_EQ(active_stream->decoderFiltersForTest().end(),
            active_stream->nextDecoderHeaderFilterForTest());
}

TEST_F(FilterTest, ActiveStreamSingleFrameFiltersContinueDecodingAfterSendLocalReply) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_2 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0},
                          {"mock_1", mock_stream_filter_1},
                          {"mock_2", mock_stream_filter_2}};

  initializeFilter();

  EXPECT_CALL(*mock_stream_filter_0, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  filter_->onDecodingSuccess(std::move(request));
  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  // Decoding is stopped when `decodeHeaderFrame` of `mock_stream_filter_0` is called.
  // Next filter is `mock_1`.
  EXPECT_EQ("mock_1", (*active_stream->nextDecoderHeaderFilterForTest())->filterConfigName());

  EXPECT_CALL(*mock_stream_filter_1, decodeHeaderFrame(_))
      .WillOnce(Invoke([&](const RequestHeaderFrame&) {
        EXPECT_CALL(*server_codec_, encode(_, _)); // Local reply will be encoded and sent.
        active_stream->sendLocalReply(Status(StatusCode::kUnknown, "test_detail"), {}, nullptr);
        // Make no sense because the filter chain will be always stopped if
        // the stream is reset or completed.
        return HeaderFilterStatus::StopIteration;
      }));
  mock_stream_filter_0->decoder_callbacks_->continueDecoding();

  // Stream is reset or complete when `decodeHeaderFrame` of `mock_stream_filter_1` is called.
  // Next filter is still `mock_1` because the iterator is not increased.
  EXPECT_EQ("mock_1", (*active_stream->nextDecoderHeaderFilterForTest())->filterConfigName());

  mock_stream_filter_1->decoder_callbacks_->continueDecoding();

  // The stream is reset after `sendLocalReply` is called and continue decoding will be ignored.
  EXPECT_EQ("mock_1", (*active_stream->nextDecoderHeaderFilterForTest())->filterConfigName());

  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_local_.value(), 1);
}

TEST_F(FilterTest, FilterSendLocalReplyButRespondReturnNull) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_2 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0},
                          {"mock_1", mock_stream_filter_1},
                          {"mock_2", mock_stream_filter_2}};

  initializeFilter();

  EXPECT_CALL(*mock_stream_filter_0, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  filter_->onDecodingSuccess(std::move(request));
  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(*server_codec_, respond(_, _, _)).WillOnce(Return(ByMove(nullptr)));
  EXPECT_CALL(*mock_stream_filter_1, decodeHeaderFrame(_))
      .WillOnce(Invoke([&](const RequestHeaderFrame&) {
        // No response will be sent and stream will be reset directly.
        active_stream->sendLocalReply(Status(StatusCode::kUnknown, "test_detail"), {}, nullptr);
        // Make no sense because the filter chain will be always stopped if
        // the stream is reset or completed.
        return HeaderFilterStatus::StopIteration;
      }));
  mock_stream_filter_0->decoder_callbacks_->continueDecoding();

  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_local_.value(), 0);
}

TEST_F(FilterTest, ActiveStreamSingleFrameFiltersContinueEncoding) {
  auto mock_encoder_filter_0 = std::make_shared<NiceMock<MockEncoderFilter>>();
  auto mock_encoder_filter_1 = std::make_shared<NiceMock<MockEncoderFilter>>();
  auto mock_encoder_filter_2 = std::make_shared<NiceMock<MockEncoderFilter>>();

  mock_encoder_filters_ = {{"mock_0", mock_encoder_filter_0},
                           {"mock_1", mock_encoder_filter_1},
                           {"mock_2", mock_encoder_filter_2}};

  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(*mock_encoder_filter_2, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  // `continueEncoding` will be called in the `onResponseHeaderFrame`.
  active_stream->onResponseHeaderFrame(std::move(response));

  // Encoding is stopped when `encodeHeaderFrame` of `mock_stream_filter_2` is called.
  // Next filter is `mock_1`.
  EXPECT_EQ("mock_1", (*active_stream->nextEncoderHeaderFilterForTest())->filterConfigName());

  EXPECT_CALL(*mock_encoder_filter_1, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  mock_encoder_filter_2->encoder_callbacks_->continueEncoding();

  // Encoding is stopped when `encodeHeaderFrame` of `mock_stream_filter_1` is called.
  // Next filter is `mock_0`.
  EXPECT_EQ("mock_0", (*active_stream->nextEncoderHeaderFilterForTest())->filterConfigName());

  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));
  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingContext&) {
        Buffer::OwnedImpl buffer;
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));

  mock_encoder_filter_1->encoder_callbacks_->continueEncoding();
}

TEST_F(FilterTest, ActiveStreamSingleFrameFiltersContinueEncodingButResponseEncodingFailure) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_2 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0},
                          {"mock_1", mock_stream_filter_1},
                          {"mock_2", mock_stream_filter_2}};

  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(*mock_stream_filter_2, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  // `continueEncoding` will be called in the `onResponseHeaderFrame`.
  active_stream->onResponseHeaderFrame(std::move(response));

  // Encoding is stopped when `encodeHeaderFrame` of `mock_stream_filter_2` is called.
  // Next filter is `mock_1`.
  EXPECT_EQ("mock_1", (*active_stream->nextEncoderHeaderFilterForTest())->filterConfigName());

  EXPECT_CALL(*mock_stream_filter_1, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  mock_stream_filter_2->encoder_callbacks_->continueEncoding();

  // Encoding is stopped when `encodeHeaderFrame` of `mock_stream_filter_1` is called.
  // Next filter is `mock_0`.
  EXPECT_EQ("mock_0", (*active_stream->nextEncoderHeaderFilterForTest())->filterConfigName());

  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingContext&) -> EncodingResult {
        return absl::InvalidArgumentError("encoding-error");
      }));

  mock_stream_filter_1->encoder_callbacks_->continueEncoding();
}

TEST_F(FilterTest, ActiveStreamSingleFrameFiltersContinueEncodingAfterSendLocalReply) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_2 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0},
                          {"mock_1", mock_stream_filter_1},
                          {"mock_2", mock_stream_filter_2}};

  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(*mock_stream_filter_2, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  // `continueEncoding` will be called in the `onResponseHeaderFrame`.
  active_stream->onResponseHeaderFrame(std::move(response));

  // Encoding is stopped when `encodeHeaderFrame` of `mock_stream_filter_2` is called.
  // Next filter is `mock_1`.
  EXPECT_EQ("mock_1", (*active_stream->nextEncoderHeaderFilterForTest())->filterConfigName());

  EXPECT_CALL(*mock_stream_filter_1, encodeHeaderFrame(_))
      .WillOnce(Invoke([&](const ResponseHeaderFrame&) {
        EXPECT_CALL(*server_codec_, encode(_, _));
        active_stream->sendLocalReply(Status(StatusCode::kUnknown, "test_detail"), {}, nullptr);
        // Make no sense because the filter chain will be always stopped if
        // the stream is reset or completed.
        return HeaderFilterStatus::StopIteration;
      }));

  mock_stream_filter_2->encoder_callbacks_->continueEncoding();

  // Encoding will be stopped when `encodeHeaderFrame` of `mock_stream_filter_1` is called.
  // Next filter is still `mock_1` because the iterator is not increased.
  EXPECT_EQ("mock_1", (*active_stream->nextEncoderHeaderFilterForTest())->filterConfigName());

  mock_stream_filter_1->encoder_callbacks_->continueEncoding();

  // The stream is reset after `sendLocalReply` is called and continue encoding will be ignored.
  EXPECT_EQ("mock_1", (*active_stream->nextEncoderHeaderFilterForTest())->filterConfigName());
}

TEST_F(FilterTest, ActiveStreamMultipleFrameFiltersContinueDecoding) {
  NiceMock<MockRequestFramesHandler> mock_request_frames_handler;

  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  EXPECT_CALL(*mock_stream_filter_1, setDecoderFilterCallbacks(_)).WillOnce(Invoke([&](auto& cb) {
    mock_stream_filter_1->decoder_callbacks_ = &cb;
    cb.setRequestFramesHandler(&mock_request_frames_handler);
  }));

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0}, {"mock_1", mock_stream_filter_1}};

  initializeFilter();

  EXPECT_CALL(*mock_stream_filter_0, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  request->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  auto request_frame_0 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  request_frame_0->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  auto request_frame_1 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  request_frame_1->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  auto request_frame_2 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();

  filter_->onDecodingSuccess(std::move(request));
  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  // Decoding is stopped when `decodeHeaderFrame` of `mock_stream_filter_0` is called.
  // Next filter is `mock_1`.
  EXPECT_EQ("mock_1", (*active_stream->nextDecoderHeaderFilterForTest())->filterConfigName());

  // The common frame will be pending until the header frame is completed.
  filter_->onDecodingSuccess(std::move(request_frame_0));

  // The StopIteration will be returned but it will be ignored because this is the last filter.
  EXPECT_CALL(*mock_stream_filter_1, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration)); // StopIteration will be ignored.
  EXPECT_CALL(*mock_stream_filter_1, decodeCommonFrame(_))
      .WillOnce(Return(CommonFilterStatus::StopIteration))  // StopIteration will be ignored.
      .WillOnce(Return(CommonFilterStatus::StopIteration))  // StopIteration will be ignored.
      .WillOnce(Return(CommonFilterStatus::StopIteration)); // StopIteration will be ignored.

  // The first common frame will be handled by the first filter and StopIteration will be returned.
  EXPECT_CALL(*mock_stream_filter_0, decodeCommonFrame(_))
      .WillOnce(Return(CommonFilterStatus::StopIteration)) // First common frame will be stopped.
      .WillOnce(Return(CommonFilterStatus::Continue))      // Second common frame will be continued.
      .WillOnce(Return(CommonFilterStatus::Continue));     // Third common frame will be continued.

  // Filter chain for header frame will be completed and start the filter chain for common frame.
  mock_stream_filter_0->decoder_callbacks_->continueDecoding();

  // Filter chain for header frame is completed.
  EXPECT_EQ(active_stream->decoderFiltersForTest().end(),
            active_stream->nextDecoderHeaderFilterForTest());
  // Decoding is stopped when `decodeCommonFrame` of `mock_stream_filter_0` is called.
  // Next filter is `mock_1`.
  EXPECT_EQ("mock_1", (*active_stream->nextDecoderCommonFilterForTest())->filterConfigName());

  // The second common frame will be pending because the first common frame is not completed.
  filter_->onDecodingSuccess(std::move(request_frame_1));

  EXPECT_CALL(mock_request_frames_handler, onRequestCommonFrame(_)).Times(2);

  // Push the filter chain to next step. This will complete the first common frame and
  // the second common frame because the filter chain will not be stopped by the second common
  // frame.
  mock_stream_filter_0->decoder_callbacks_->continueDecoding();

  // The previous two common frames are completed. All related states are reset.
  EXPECT_EQ("mock_0", (*active_stream->nextDecoderCommonFilterForTest())->filterConfigName());

  EXPECT_CALL(mock_request_frames_handler, onRequestCommonFrame(_));
  // The last common frame will be handled directly because all the previous common frames are
  // completed and the filter chain will not be stopped by the last common frame.
  filter_->onDecodingSuccess(std::move(request_frame_2));
}

TEST_F(FilterTest, ActiveStreamMultipleFrameFiltersContinueEncoding) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0}, {"mock_1", mock_stream_filter_1}};

  initializeFilter();

  EXPECT_CALL(*mock_stream_filter_0, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  auto response_frame_0 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  response_frame_0->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  auto response_frame_1 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  response_frame_1->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  auto response_frame_2 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();

  EXPECT_CALL(*mock_stream_filter_1, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  active_stream->onResponseHeaderFrame(std::move(response));

  // Encoding is stopped when `encodeHeaderFrame` of `mock_stream_filter_1` is called.
  // Next filter is `mock_0`.
  EXPECT_EQ("mock_0", (*active_stream->nextEncoderHeaderFilterForTest())->filterConfigName());

  // The common frame will be pending until the header frame is completed.
  active_stream->onResponseCommonFrame(std::move(response_frame_0));

  // The StopIteration will be returned but it will be ignored because this is the last filter.
  EXPECT_CALL(*mock_stream_filter_0, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration)); // StopIteration will be ignored.
  EXPECT_CALL(*mock_stream_filter_0, encodeCommonFrame(_))
      .WillOnce(Return(CommonFilterStatus::StopIteration))  // StopIteration will be ignored.
      .WillOnce(Return(CommonFilterStatus::StopIteration))  // StopIteration will be ignored.
      .WillOnce(Return(CommonFilterStatus::StopIteration)); // StopIteration will be ignored.

  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));
  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingContext&) {
        Buffer::OwnedImpl buffer;
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));

  // The first common frame will be handled by the first filter and StopIteration will be returned.
  EXPECT_CALL(*mock_stream_filter_1, encodeCommonFrame(_))
      .WillOnce(Return(CommonFilterStatus::StopIteration))
      .WillOnce(Return(CommonFilterStatus::Continue))
      .WillOnce(Return(CommonFilterStatus::Continue));

  // Filter chain for header frame will be completed and start the filter chain for common frame.
  mock_stream_filter_1->encoder_callbacks_->continueEncoding();

  // Filter chain for header frame is completed.
  EXPECT_EQ(active_stream->encoderFiltersForTest().end(),
            active_stream->nextEncoderHeaderFilterForTest());
  // Decoding is stopped when `encodeCommonFrame` of `mock_stream_filter_1` is called.
  // Next filter is `mock_0`.
  EXPECT_EQ("mock_0", (*active_stream->nextEncoderCommonFilterForTest())->filterConfigName());

  // The second common frame will be pending because the first common frame is not completed.
  active_stream->onResponseCommonFrame(std::move(response_frame_1));

  EXPECT_CALL(*server_codec_, encode(_, _))
      .Times(2)
      .WillRepeatedly(
          Invoke([](const StreamFrame&, EncodingContext&) { return EncodingResult{0}; }));

  // Push the filter chain to next step. This will complete the first common frame and
  // the second common frame because the filter chain will not be stopped by the second common
  // frame.
  mock_stream_filter_1->encoder_callbacks_->continueEncoding();

  // The previous two common frames are completed. All related states are reset.
  EXPECT_EQ("mock_1", (*active_stream->nextEncoderCommonFilterForTest())->filterConfigName());

  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([](const StreamFrame&, EncodingContext&) { return EncodingResult{0}; }));

  // The last common frame will be handled directly because all the previous common frames are
  // completed.
  active_stream->onResponseCommonFrame(std::move(response_frame_2));
}

TEST_F(FilterTest, UpstreamResponseAfterPreviousUpstreamResponse) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0}, {"mock_1", mock_stream_filter_1}};

  // The logger is used to test the log format.
  initializeFilter(false, loggerFormFormat());

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 0);

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // Response filter chain is stopped by the first filter.
  EXPECT_CALL(*mock_stream_filter_1, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  active_stream->onResponseHeaderFrame(std::move(response));

  // The repeated response will result in the stream reset.
  auto response_again = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  active_stream->onResponseHeaderFrame(std::move(response_again));

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 1);
}

TEST_F(FilterTest, UpstreamResponseAfterPreviousLocalReply) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0}, {"mock_1", mock_stream_filter_1}};

  // The logger is used to test the log format.
  initializeFilter(false, loggerFormFormat());

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 0);

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // Response filter chain of local reply is stopped by the first filter.
  EXPECT_CALL(*mock_stream_filter_1, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  active_stream->sendLocalReply(Status(StatusCode::kUnknown, "test_detail"), {}, nullptr);

  // The repeated response will result in the stream reset.
  auto response_again = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  active_stream->onResponseHeaderFrame(std::move(response_again));

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);
  // Although the original local reply is not sent to the downstream, it will still be counted as an
  // error reply. This is corner case.
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 1);
}

TEST_F(FilterTest, SendLocalReplyAfterPreviousLocalReply) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0}, {"mock_1", mock_stream_filter_1}};

  // The logger is used to test the log format.
  initializeFilter(false, loggerFormFormat());

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 0);

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // Response filter chain of local reply is stopped by the first filter.
  EXPECT_CALL(*mock_stream_filter_1, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::StopIteration));

  active_stream->sendLocalReply(Status(StatusCode::kUnknown, "test_detail_1"), {}, nullptr);

  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame& stream, EncodingContext&) {
        auto* typed_response = dynamic_cast<const FakeStreamCodecFactory::FakeResponse*>(&stream);
        EXPECT_EQ(typed_response->status_.code(), static_cast<uint32_t>(StatusCode::kUnknown));
        EXPECT_EQ(typed_response->message_, "test_detail_2");

        Buffer::OwnedImpl buffer;
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));

  // The latest local reply will skip the filter chain processing and be sent to the downstream
  // directly.
  active_stream->sendLocalReply(Status(StatusCode::kUnknown, "test_detail_2"), {}, nullptr);

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 0);
}

TEST_F(FilterTest, SendLocalReplyAfterPreviousUpstreamResponseHeaderIsSent) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0}, {"mock_1", mock_stream_filter_1}};

  // The logger is used to test the log format.
  initializeFilter(false, loggerFormFormat());

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 0);

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  testing::Sequence s;

  // Filter chain is completed and the response header is sent to the downstream.
  EXPECT_CALL(*mock_stream_filter_1, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_0, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));

  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame& stream, EncodingContext&) {
        auto* typed_response = dynamic_cast<const FakeStreamCodecFactory::FakeResponse*>(&stream);
        EXPECT_EQ(typed_response->status_.code(), 0);
        EXPECT_EQ(typed_response->message_, "anything");

        Buffer::OwnedImpl buffer;
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response->message_ = "anything";
  // The response header is not end frame to ensure the stream won't be cleared after the response
  // header is sent.
  response->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);

  active_stream->onResponseHeaderFrame(std::move(response));

  // The latest local reply will result in the stream reset because the previous response header is
  // sent.
  active_stream->sendLocalReply(Status(StatusCode::kUnknown, "test_detail_2"), {}, nullptr);

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 1);
}

TEST_F(FilterTest, ActiveStreamSendLocalReply) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_2 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0},
                          {"mock_1", mock_stream_filter_1},
                          {"mock_2", mock_stream_filter_2}};

  initializeFilter(false, loggerFormFormat());

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  request->host_ = "host-value";
  request->path_ = "/path-value";
  request->method_ = "method-value";
  request->protocol_ = "protocol-value";
  request->data_["request-key"] = "request-value";

  testing::InSequence s;

  // Decoder filter chain processing.
  EXPECT_CALL(*mock_stream_filter_0, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_1, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_2, decodeHeaderFrame(_)).WillOnce(Invoke([&](auto&) {
    mock_stream_filter_2->decoder_callbacks_->sendLocalReply(
        Status(StatusCode::kUnknown, "test_detail"), {},
        [](StreamResponse& response) { response.set("response-key", "response-value"); });
    return HeaderFilterStatus::StopIteration;
  }));

  // Encoder filter chain processing.
  EXPECT_CALL(*mock_stream_filter_2, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_1, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_0, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));

  // Send reply to the downstream.
  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame& response, EncodingContext&) {
        Buffer::OwnedImpl buffer;
        EXPECT_EQ(dynamic_cast<const Response*>(&response)->status().code(),
                  static_cast<uint32_t>(StatusCode::kUnknown));
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));

  // Clean up the stream and log the access log.
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_CALL(*factory_context_.server_factory_context_.access_log_manager_.file_,
              write("host-value /path-value method-value protocol-value request-value "
                    "response-value - 2 test_detail"));

  // Check the drain manager.
  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));

  filter_->onDecodingSuccess(std::move(request));

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_local_.value(), 1);
}

TEST_F(FilterTest, ActiveStreamSendLocalReplyWhenProcessingBody) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_2 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0},
                          {"mock_1", mock_stream_filter_1},
                          {"mock_2", mock_stream_filter_2}};

  initializeFilter(false, loggerFormFormat());

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  request->host_ = "host-value";
  request->path_ = "/path-value";
  request->method_ = "method-value";
  request->protocol_ = "protocol-value";
  request->data_["request-key"] = "request-value";
  request->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);

  testing::InSequence s1;
  // Decoder filter chain processing for header frame.
  EXPECT_CALL(*mock_stream_filter_0, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_1, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_2, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));

  filter_->onDecodingSuccess(std::move(request));

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_local_.value(), 0);

  testing::InSequence s2;
  // Decoder filter chain processing for common frame.
  EXPECT_CALL(*mock_stream_filter_0, decodeCommonFrame(_))
      .WillOnce(Return(CommonFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_1, decodeCommonFrame(_))
      .WillOnce(Return(CommonFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_2, decodeCommonFrame(_)).WillOnce(Invoke([&](auto&) {
    mock_stream_filter_2->decoder_callbacks_->sendLocalReply(
        Status(StatusCode::kUnknown, "test_detail"), {},
        [](StreamResponse& response) { response.set("response-key", "response-value"); });
    return CommonFilterStatus::StopIteration;
  }));

  // Encoder filter chain processing.
  EXPECT_CALL(*mock_stream_filter_2, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_1, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_0, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));

  // Send reply to the downstream.
  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame& response, EncodingContext&) {
        Buffer::OwnedImpl buffer;
        EXPECT_EQ(dynamic_cast<const Response*>(&response)->status().code(),
                  static_cast<uint32_t>(StatusCode::kUnknown));
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));

  // Clean up the stream and log the access log.
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_CALL(*factory_context_.server_factory_context_.access_log_manager_.file_,
              write("host-value /path-value method-value protocol-value request-value "
                    "response-value - 2 test_detail"));

  // Check the drain manager.
  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));

  auto request_frame = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  filter_->onDecodingSuccess(std::move(request_frame));

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_local_.value(), 1);
}

TEST_F(FilterTest, ActiveStreamSendLocalReplyWhenTransferringBody) {
  NiceMock<MockRequestFramesHandler> mock_request_frames_handler;

  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_2 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {{"mock_0", mock_stream_filter_0},
                          {"mock_1", mock_stream_filter_1},
                          {"mock_2", mock_stream_filter_2}};

  // Set the request handler.
  ON_CALL(*mock_stream_filter_2, setDecoderFilterCallbacks(_)).WillByDefault(Invoke([&](auto& cb) {
    mock_stream_filter_2->decoder_callbacks_ = &cb;
    cb.setRequestFramesHandler(&mock_request_frames_handler);
  }));

  initializeFilter(false, loggerFormFormat());

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  request->host_ = "host-value";
  request->path_ = "/path-value";
  request->method_ = "method-value";
  request->protocol_ = "protocol-value";
  request->data_["request-key"] = "request-value";
  request->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);

  testing::InSequence s1;
  // Decoder filter chain processing for header frame.
  EXPECT_CALL(*mock_stream_filter_0, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_1, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_2, decodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));

  filter_->onDecodingSuccess(std::move(request));

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_local_.value(), 0);

  testing::InSequence s2;
  // Decoder filter chain processing for common frame.
  EXPECT_CALL(*mock_stream_filter_0, decodeCommonFrame(_))
      .WillOnce(Return(CommonFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_1, decodeCommonFrame(_))
      .WillOnce(Return(CommonFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_2, decodeCommonFrame(_))
      .WillOnce(Return(CommonFilterStatus::StopIteration));

  EXPECT_CALL(mock_request_frames_handler, onRequestCommonFrame(_)).WillOnce(Invoke([&](auto&&) {
    mock_stream_filter_2->decoder_callbacks_->sendLocalReply(
        Status(StatusCode::kUnknown, "test_detail"), {},
        [](StreamResponse& response) { response.set("response-key", "response-value"); });
  }));

  // Encoder filter chain processing.
  EXPECT_CALL(*mock_stream_filter_2, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_1, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));
  EXPECT_CALL(*mock_stream_filter_0, encodeHeaderFrame(_))
      .WillOnce(Return(HeaderFilterStatus::Continue));

  // Send reply to the downstream.
  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame& response, EncodingContext&) {
        Buffer::OwnedImpl buffer;
        EXPECT_EQ(dynamic_cast<const Response*>(&response)->status().code(),
                  static_cast<uint32_t>(StatusCode::kUnknown));
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));

  // Clean up the stream and log the access log.
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_CALL(*factory_context_.server_factory_context_.access_log_manager_.file_,
              write("host-value /path-value method-value protocol-value request-value "
                    "response-value - 2 test_detail"));

  // Check the drain manager.
  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));

  auto request_frame = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  filter_->onDecodingSuccess(std::move(request_frame));

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_local_.value(), 1);
}

TEST_F(FilterTest, ActiveStreamCompleteDirectly) {
  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  active_stream->completeStream();

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);

  EXPECT_EQ(0, filter_->activeStreamsForTest().size());
}

TEST_F(FilterTest, ActiveStreamCompleteDirectlyFromFilter) {
  mock_stream_filters_.push_back({"mock_0", std::make_shared<NiceMock<MockStreamFilter>>()});

  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  active_stream->decoderFiltersForTest()[0]->completeDirectly();

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);

  EXPECT_EQ(0, filter_->activeStreamsForTest().size());
}

TEST_F(FilterTest, NewStreamAndReplyNormally) {
  auto mock_decoder_filter_0 = std::make_shared<NiceMock<MockDecoderFilter>>();
  mock_decoder_filters_ = {{"mock_0", mock_decoder_filter_0}};

  // The logger is used to test the log format.
  initializeFilter(false, loggerFormFormat());

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  request->host_ = "host-value";
  request->path_ = "/path-value";
  request->method_ = "method-value";
  request->protocol_ = "protocol-value";
  request->data_["request-key"] = "request-value";

  EXPECT_CALL(*mock_decoder_filter_0, decodeHeaderFrame(_)).WillOnce(Invoke([&](auto&) {
    auto cb = mock_decoder_filter_0->decoder_callbacks_;
    EXPECT_EQ(&cb->activeSpan(), &Tracing::NullSpan::instance());
    EXPECT_FALSE(cb->tracingConfig().has_value());
    return HeaderFilterStatus::Continue;
  }));

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 0);

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));

  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingContext&) {
        Buffer::OwnedImpl buffer;
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));

  EXPECT_CALL(*factory_context_.server_factory_context_.access_log_manager_.file_,
              write("host-value /path-value method-value protocol-value request-value "
                    "response-value - 0 via_upstream"));

  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response->status_ = {0, true};
  response->data_["response-key"] = "response-value";

  active_stream->onResponseHeaderFrame(std::move(response));

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_reset_.value(), 0);
  EXPECT_EQ(
      factory_context_.store_.counter("generic_proxy.test_prefix.downstream_rq_code.0").value(), 1);
}

TEST_F(FilterTest, NewStreamAndReplyNormallyWithMultipleFrames) {
  auto mock_decoder_filter_0 = std::make_shared<NiceMock<MockDecoderFilter>>();
  mock_decoder_filters_ = {{"mock_0", mock_decoder_filter_0}};

  NiceMock<MockRequestFramesHandler> mock_stream_frame_handler;

  EXPECT_CALL(*mock_decoder_filter_0, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&mock_stream_frame_handler](DecoderFilterCallback& callbacks) {
        callbacks.setRequestFramesHandler(&mock_stream_frame_handler);
      }));

  // The logger is used to test the log format.
  initializeFilter(false, loggerFormFormat());

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  request->host_ = "host-value";
  request->path_ = "/path-value";
  request->method_ = "method-value";
  request->protocol_ = "protocol-value";
  request->data_["request-key"] = "request-value";
  request->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);

  // The first frame is not the end stream and we will create a frame handler for it.
  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());
  EXPECT_EQ(1, filter_->frameHandlersForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 0);

  // stream_frame_handler will be called twice to handle the two frames (except the first
  // StreamRequest frame).
  EXPECT_CALL(mock_stream_frame_handler, onRequestCommonFrame(_)).Times(2);

  auto request_frame_1 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  request_frame_1->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  filter_->onDecodingSuccess(std::move(request_frame_1));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());
  EXPECT_EQ(1, filter_->frameHandlersForTest().size());

  // When the last frame is the end stream, we will delete the frame handler.
  auto request_frame_2 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  request_frame_2->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_END_STREAM);
  filter_->onDecodingSuccess(std::move(request_frame_2));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());
  EXPECT_EQ(0, filter_->frameHandlersForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(*factory_context_.server_factory_context_.access_log_manager_.file_,
              write("host-value /path-value method-value protocol-value request-value "
                    "response-value - 123 via_upstream"));

  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false)).Times(2);
  EXPECT_CALL(*server_codec_, encode(_, _))
      .Times(2)
      .WillRepeatedly(Invoke([&](const StreamFrame&, EncodingContext&) {
        Buffer::OwnedImpl buffer;
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response->data_["response-key"] = "response-value";
  response->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_EMPTY);
  response->status_ = {123, false}; // Response non-OK.

  active_stream->onResponseHeaderFrame(std::move(response));

  auto response_frame_1 = std::make_unique<FakeStreamCodecFactory::FakeCommonFrame>();
  response_frame_1->stream_frame_flags_ = FrameFlags(0, FrameFlags::FLAG_END_STREAM);

  active_stream->onResponseCommonFrame(std::move(response_frame_1));

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 1);
  EXPECT_EQ(
      factory_context_.store_.counter("generic_proxy.test_prefix.downstream_rq_code.123").value(),
      1);
}

TEST_F(FilterTest, NewStreamAndReplyNormallyWithDrainClose) {
  auto mock_decoder_filter_0 = std::make_shared<NiceMock<MockDecoderFilter>>();
  mock_decoder_filters_ = {{"mock_0", mock_decoder_filter_0}};

  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 0);

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));

  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingContext&) {
        Buffer::OwnedImpl buffer;
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));

  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(true));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response->status_ = {234, false}; // Response non-OK.
  active_stream->streamInfo().setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamProtocolError);
  active_stream->onResponseHeaderFrame(std::move(response));

  EXPECT_EQ(filter_config_->stats().downstream_rq_total_.value(), 1);
  EXPECT_EQ(filter_config_->stats().downstream_rq_active_.value(), 0);
  EXPECT_EQ(filter_config_->stats().downstream_rq_error_.value(), 1);
  EXPECT_EQ(
      factory_context_.store_.counter("generic_proxy.test_prefix.downstream_rq_code.234").value(),
      1);
  EXPECT_EQ(
      factory_context_.store_.counter("generic_proxy.test_prefix.downstream_rq_flag.UPE").value(),
      1);
}

TEST_F(FilterTest, NewStreamAndReplyNormallyWithStreamDrainClose) {
  auto mock_decoder_filter_0 = std::make_shared<NiceMock<MockDecoderFilter>>();
  mock_decoder_filters_ = {{"mock_0", mock_decoder_filter_0}};

  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));

  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingContext&) {
        Buffer::OwnedImpl buffer;
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));

  // The drain close of factory_context_.drain_manager_ is false, but the drain close of
  // active_stream is true.
  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  response->stream_frame_flags_ =
      FrameFlags(0, FrameFlags::FLAG_END_STREAM | FrameFlags::FLAG_DRAIN_CLOSE);
  active_stream->onResponseHeaderFrame(std::move(response));
}

TEST_F(FilterTest, NewStreamAndReplyNormallyWithTracing) {
  auto mock_decoder_filter_0 = std::make_shared<NiceMock<MockDecoderFilter>>();
  mock_decoder_filters_ = {{"mock_0", mock_decoder_filter_0}};

  initializeFilter(true);

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, Tracing::TraceContext&,
                     const StreamInfo::StreamInfo&, const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());
            return span;
          }));

  EXPECT_CALL(*mock_decoder_filter_0, decodeHeaderFrame(_))
      .WillOnce(Invoke([&](const RequestHeaderFrame&) {
        EXPECT_NE(&mock_decoder_filter_0->decoder_callbacks_->activeSpan(),
                  &Tracing::NullSpan::instance());
        EXPECT_TRUE(mock_decoder_filter_0->decoder_callbacks_->tracingConfig().has_value());
        EXPECT_EQ(mock_decoder_filter_0->decoder_callbacks_->tracingConfig()->maxPathTagLength(),
                  256);
        EXPECT_EQ(mock_decoder_filter_0->decoder_callbacks_->tracingConfig()->spawnUpstreamSpan(),
                  true);
        return HeaderFilterStatus::Continue;
      }));

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(*span, finishSpan());

  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));

  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingContext&) {
        Buffer::OwnedImpl buffer;
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));

  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  active_stream->onResponseHeaderFrame(std::move(response));
}

TEST_F(FilterTest, NewStreamAndReplyNormallyWithTracingAndSamplingToTrue) {
  auto mock_decoder_filter_0 = std::make_shared<NiceMock<MockDecoderFilter>>();
  mock_decoder_filters_ = {{"mock_0", mock_decoder_filter_0}};

  initializeFilter(true);

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  auto* span = new NiceMock<Tracing::MockSpan>();
  EXPECT_CALL(*tracer_, startSpan_(_, _, _, _))
      .WillOnce(
          Invoke([&](const Tracing::Config& config, Tracing::TraceContext&,
                     const StreamInfo::StreamInfo&, const Tracing::Decision) -> Tracing::Span* {
            EXPECT_EQ(Tracing::OperationName::Egress, config.operationName());
            return span;
          }));

  EXPECT_CALL(*mock_decoder_filter_0, decodeHeaderFrame(_)).WillOnce(Invoke([&](auto&) {
    auto cb = mock_decoder_filter_0->decoder_callbacks_;

    EXPECT_NE(&cb->activeSpan(), &Tracing::NullSpan::instance());
    EXPECT_TRUE(cb->tracingConfig().has_value());
    EXPECT_EQ(cb->tracingConfig()->maxPathTagLength(), 256);
    EXPECT_EQ(cb->tracingConfig()->spawnUpstreamSpan(), true);
    return HeaderFilterStatus::Continue;
  }));

  EXPECT_CALL(factory_context_.server_factory_context_.runtime_loader_.snapshot_,
              featureEnabled("tracing.random_sampling",
                             testing::An<const envoy::type::v3::FractionalPercent&>()))
      .WillOnce(Return(true));

  filter_->onDecodingSuccess(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(*span, setTag(_, _)).Times(testing::AnyNumber());
  EXPECT_CALL(*span, finishSpan());

  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));

  EXPECT_CALL(*server_codec_, encode(_, _))
      .WillOnce(Invoke([&](const StreamFrame&, EncodingContext&) {
        Buffer::OwnedImpl buffer;
        buffer.add("test");

        server_codec_callbacks_->writeToConnection(buffer);
        buffer.drain(buffer.length());

        return EncodingResult{4};
      }));

  EXPECT_CALL(factory_context_.drain_manager_, drainClose()).WillOnce(Return(false));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  active_stream->onResponseHeaderFrame(std::move(response));
}

} // namespace
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
