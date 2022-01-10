#include <memory>
#include <string>
#include <utility>

#include "source/extensions/filters/network/meta_protocol_proxy/proxy.h"

#include "test/extensions/filters/network/meta_protocol_proxy/fake_codec.h"
#include "test/extensions/filters/network/meta_protocol_proxy/mocks/codec.h"
#include "test/extensions/filters/network/meta_protocol_proxy/mocks/filter.h"
#include "test/extensions/filters/network/meta_protocol_proxy/mocks/route.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/registry.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using testing::ByMove;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {
namespace {

/**
 * Test creating codec factory from typed extension config.
 */
TEST(BasicFilterConfigTest, CreatingCodecFactory) {
  const std::string yaml_config = R"EOF(
    name: envoy.meta_protocol_proxy.codec.fake
  )EOF";
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  envoy::config::core::v3::TypedExtensionConfig proto_config;
  TestUtility::loadFromYaml(yaml_config, proto_config);

  {
    EXPECT_THROW(FilterConfig::codecFactoryFromProto(proto_config, factory_context),
                 EnvoyException);
  }

  {
    FakeStreamCodecFactoryConfig codec_factory_config;
    Registry::InjectFactory<CodecFactoryConfig> registration(codec_factory_config);

    EXPECT_NE(nullptr, FilterConfig::codecFactoryFromProto(proto_config, factory_context));
  }
}

/**
 * Test creating route matcher from proto config.
 */
TEST(BasicFilterConfigTest, CreatingRouteMatcher) {
  static const std::string yaml_config = R"EOF(
    name: test_matcher_tree
    routes:
      matcher_list:
        matchers:
        - predicate:
            and_matcher:
              predicate:
              - single_predicate:
                  input:
                    name: envoy.matching.meta_protocol.input.service
                    typed_config:
                      "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.v3.ServiceMatchInput
                  value_match:
                    exact: "service_0"
          on_match:
            action:
              name: envoy.matching.action.meta_protocol.route
              typed_config:
                "@type": type.googleapis.com/envoy.extensions.filters.network.meta_protocol_proxy.matcher.action.v3.RouteAction
                cluster: "cluster_0"
                metadata:
                  filter_metadata:
                    mock_filter:
                      key_0: value_0
    )EOF";
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  ProtoRouteConfiguration proto_config;
  TestUtility::loadFromYaml(yaml_config, proto_config);

  EXPECT_NE(nullptr, FilterConfig::routeMatcherFromProto(proto_config, factory_context));
}

/**
 * Test creating L7 filter factories from proto config.
 */
TEST(BasicFilterConfigTest, CreatingFilterFactories) {
  NiceMock<Server::Configuration::MockFactoryContext> factory_context;

  ProtobufWkt::RepeatedPtrField<envoy::config::core::v3::TypedExtensionConfig> filters_proto_config;

  auto filter_0 = filters_proto_config.Add();
  filter_0->set_name("mock_meta_protocol_filter_name_0");
  auto filter_1 = filters_proto_config.Add();
  filter_1->set_name("mock_meta_protocol_filter_name_1");

  NiceMock<MockStreamFilterConfig> mock_filter_config_0;
  NiceMock<MockStreamFilterConfig> mock_filter_config_1;

  ON_CALL(mock_filter_config_0, name()).WillByDefault(Return("mock_meta_protocol_filter_name_0"));
  ON_CALL(mock_filter_config_1, name()).WillByDefault(Return("mock_meta_protocol_filter_name_1"));

  Registry::InjectFactory<NamedFilterConfigFactory> registration_0(mock_filter_config_0);
  Registry::InjectFactory<NamedFilterConfigFactory> registration_1(mock_filter_config_1);

  // No terminal filter.
  {
    EXPECT_THROW_WITH_MESSAGE(
        FilterConfig::filtersFactoryFromProto(filters_proto_config, "test", factory_context),
        EnvoyException, "A terminal L7 filter is necessary for meta protocol proxy");
  }

  // Error terminal filter position.
  {
    ON_CALL(mock_filter_config_0, isTerminalFilter()).WillByDefault(Return(true));

    EXPECT_THROW_WITH_MESSAGE(
        FilterConfig::filtersFactoryFromProto(filters_proto_config, "test", factory_context),
        EnvoyException,
        "Terminal filter: mock_meta_protocol_filter_name_0 must be the last meta protocol L7 "
        "filter");
  }

  {
    ON_CALL(mock_filter_config_0, isTerminalFilter()).WillByDefault(Return(false));
    ON_CALL(mock_filter_config_1, isTerminalFilter()).WillByDefault(Return(true));
    auto factories =
        FilterConfig::filtersFactoryFromProto(filters_proto_config, "test", factory_context);
    EXPECT_EQ(2, factories.size());
  }
}

class FilterConfigTest : public testing::Test {
public:
  void initializeFilterConfig() {
    std::vector<FilterFactoryCb> factories;

    for (const auto& filter : mock_stream_filters_) {
      factories.push_back([f = filter](FilterChainFactoryCallbacks& cb) {
        ASSERT(f->isDualFilter());
        cb.addFilter(f);
      });
    }

    for (const auto& filter : mock_decoder_filters_) {
      factories.push_back([f = filter](FilterChainFactoryCallbacks& cb) {
        ASSERT(!f->isDualFilter());
        cb.addDecoderFilter(f);
      });
    }
    for (const auto& filter : mock_encoder_filters_) {
      factories.push_back([f = filter](FilterChainFactoryCallbacks& cb) {
        ASSERT(!f->isDualFilter());
        cb.addEncoderFilter(f);
      });
    }

    auto codec_factory = std::make_unique<NiceMock<MockCodecFactory>>();
    codec_factory_ = codec_factory.get();

    auto route_matcher = std::make_unique<NiceMock<MockRouteMatcher>>();
    route_matcher_ = route_matcher.get();

    mock_route_entry_ = std::make_shared<NiceMock<MockRouteEntry>>();

    filter_config_ =
        std::make_shared<FilterConfig>("test_prefix", std::move(codec_factory),
                                       std::move(route_matcher), factories, factory_context_);
  }

  std::shared_ptr<FilterConfig> filter_config_;
  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;

  NiceMock<MockCodecFactory>* codec_factory_;
  NiceMock<MockRouteMatcher>* route_matcher_;

  std::vector<std::shared_ptr<NiceMock<MockStreamFilter>>> mock_stream_filters_;
  std::vector<std::shared_ptr<NiceMock<MockDecoderFilter>>> mock_decoder_filters_;
  std::vector<std::shared_ptr<NiceMock<MockEncoderFilter>>> mock_encoder_filters_;

  std::shared_ptr<NiceMock<MockRouteEntry>> mock_route_entry_;
};

/**
 * Test the method that to get route entry.
 */
TEST_F(FilterConfigTest, RouteEntry) {
  initializeFilterConfig();

  EXPECT_CALL(*route_matcher_, routeEntry(_)).WillOnce(Return(mock_route_entry_));

  FakeStreamCodecFactory::FakeRequest fake_request;

  EXPECT_EQ(filter_config_->routeEntry(fake_request).get(), mock_route_entry_.get());
}

/**
 * Test creating an L7 filter chain.
 */
TEST_F(FilterConfigTest, CreateFilterChain) {
  auto mock_stream_filter = std::make_shared<NiceMock<MockStreamFilter>>();
  mock_stream_filters_ = {mock_stream_filter, mock_stream_filter, mock_stream_filter};

  initializeFilterConfig();

  NiceMock<MockFilterChainFactoryCallbacks> cb;

  EXPECT_CALL(cb, addFilter(_)).Times(3).WillRepeatedly(Invoke([&](StreamFilterSharedPtr filter) {
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
  void initializeFilter() {
    FilterConfigTest::initializeFilterConfig();

    auto encoder = std::make_unique<NiceMock<MockResponseEncoder>>();
    encoder_ = encoder.get();
    EXPECT_CALL(*codec_factory_, responseEncoder()).WillOnce(Return(ByMove(std::move(encoder))));

    auto decoder = std::make_unique<NiceMock<MockRequestDecoder>>();
    decoder_ = decoder.get();
    EXPECT_CALL(*codec_factory_, requestDecoder()).WillOnce(Return(ByMove(std::move(decoder))));

    auto creator = std::make_unique<NiceMock<MockMessageCreator>>();
    creator_ = creator.get();
    EXPECT_CALL(*codec_factory_, messageCreator()).WillOnce(Return(ByMove(std::move(creator))));

    EXPECT_CALL(*decoder_, setDecoderCallback(_))
        .WillOnce(
            Invoke([this](RequestDecoderCallback& callback) { decoder_callback_ = &callback; }));

    filter_ = std::make_shared<Filter>(filter_config_, factory_context_);

    EXPECT_EQ(filter_.get(), decoder_callback_);

    filter_->initializeReadFilterCallbacks(filter_callbacks_);
  }

  std::shared_ptr<Filter> filter_;

  RequestDecoderCallback* decoder_callback_{};

  NiceMock<MockRequestDecoder>* decoder_;
  NiceMock<MockResponseEncoder>* encoder_;
  NiceMock<MockMessageCreator>* creator_;

  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
};

TEST_F(FilterTest, SimpleOnNewConnection) {
  initializeFilter();
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onNewConnection());
}

TEST_F(FilterTest, SimpleOnData) {
  initializeFilter();

  Buffer::OwnedImpl fake_empty_buffer;

  EXPECT_CALL(*decoder_, decode(_));
  filter_->onData(fake_empty_buffer, false);
}

TEST_F(FilterTest, OnDecodingFailureWithoutActiveStreams) {
  initializeFilter();

  Buffer::OwnedImpl fake_empty_buffer;

  EXPECT_CALL(*decoder_, decode(_));
  filter_->onData(fake_empty_buffer, false);

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  decoder_callback_->onDecodingFailure();
}

TEST_F(FilterTest, OnDecodingSuccessWithNormalRequest) {
  auto mock_stream_filter = std::make_shared<NiceMock<MockStreamFilter>>();
  mock_stream_filters_ = {mock_stream_filter, mock_stream_filter, mock_stream_filter};

  initializeFilter();

  Buffer::OwnedImpl fake_empty_buffer;

  EXPECT_CALL(*decoder_, decode(_));
  filter_->onData(fake_empty_buffer, false);

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  // Three mock factories was added.
  EXPECT_CALL(*mock_stream_filter, onStreamDecoded(_)).Times(3);

  decoder_callback_->onDecodingSuccess(std::move(request));

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

  // Return directly.
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(fake_empty_buffer, false));
}

TEST_F(FilterTest, SendReplyDownstream) {
  initializeFilter();

  NiceMock<MockResponseEncoderCallback> encoder_callback;

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();

  Buffer::OwnedImpl response_buffer;

  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), false));

  EXPECT_CALL(encoder_callback, onEncodingSuccess(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool close_connection) {
        filter_callbacks_.connection_.write(buffer, close_connection);
      }));

  EXPECT_CALL(*encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Response&, ResponseEncoderCallback& callback) {
        Buffer::OwnedImpl buffer;
        buffer.add("test");
        callback.onEncodingSuccess(buffer, false);
      }));

  filter_->sendReplyDownstream(*response, encoder_callback);
}

TEST_F(FilterTest, GetConnection) {
  initializeFilter();

  EXPECT_EQ(&(filter_callbacks_.connection_), &filter_->connection());
}

TEST_F(FilterTest, GetFactoryContext) {
  initializeFilter();
  EXPECT_EQ(&factory_context_, &filter_->factoryContext());
}

TEST_F(FilterTest, NewStreamAndResetStream) {
  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->newDownstreamRequest(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  active_stream->resetStream();

  EXPECT_EQ(0, filter_->activeStreamsForTest().size());
}

TEST_F(FilterTest, OnDecodingFailureWithActiveStreams) {
  initializeFilter();

  auto request_0 = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  filter_->newDownstreamRequest(std::move(request_0));

  auto request_1 = std::make_unique<FakeStreamCodecFactory::FakeRequest>();
  filter_->newDownstreamRequest(std::move(request_1));

  EXPECT_EQ(2, filter_->activeStreamsForTest().size());

  Buffer::OwnedImpl fake_empty_buffer;
  EXPECT_CALL(*decoder_, decode(_));
  filter_->onData(fake_empty_buffer, false);

  EXPECT_CALL(filter_callbacks_.connection_, close(_));
  decoder_callback_->onDecodingFailure();

  EXPECT_EQ(0, filter_->activeStreamsForTest().size());
}

TEST_F(FilterTest, ActiveStreamRouteEntry) {
  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->newDownstreamRequest(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  active_stream->continueDecoding();
  EXPECT_EQ(active_stream->routeEntry(), route_matcher_->route_entry_.get());
}

TEST_F(FilterTest, ActiveStreamAddFilters) {
  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->newDownstreamRequest(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_EQ(0, active_stream->decoderFiltersForTest().size());
  EXPECT_EQ(0, active_stream->encoderFiltersForTest().size());

  EXPECT_EQ(0, active_stream->nextDecoderFilterIndexForTest());
  EXPECT_EQ(0, active_stream->nextEncoderFilterIndexForTest());

  auto new_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto new_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto new_filter_3 = std::make_shared<NiceMock<MockStreamFilter>>();

  active_stream->addDecoderFilter(new_filter_0);
  active_stream->addEncoderFilter(new_filter_0);

  active_stream->addDecoderFilter(new_filter_1);
  active_stream->addFilter(new_filter_3);

  EXPECT_EQ(3, active_stream->decoderFiltersForTest().size());
  EXPECT_EQ(2, active_stream->encoderFiltersForTest().size());

  active_stream->continueDecoding();

  EXPECT_EQ(3, active_stream->nextDecoderFilterIndexForTest());
  EXPECT_EQ(0, active_stream->nextEncoderFilterIndexForTest());
}

TEST_F(FilterTest, ActiveStreamFiltersContinueDecoding) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_2 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {mock_stream_filter_0, mock_stream_filter_1, mock_stream_filter_2};

  auto mock_encoder_filter = std::make_shared<NiceMock<MockEncoderFilter>>();
  mock_encoder_filters_ = {mock_encoder_filter, mock_encoder_filter, mock_encoder_filter};

  ON_CALL(*mock_stream_filter_1, onStreamDecoded(_))
      .WillByDefault(Return(FilterStatus::StopIteration));

  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->newDownstreamRequest(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_EQ(3, active_stream->decoderFiltersForTest().size());
  EXPECT_EQ(6, active_stream->encoderFiltersForTest().size());

  // Decoding will be stopped when `onStreamDecoded` of `mock_stream_filter_1` is called.
  EXPECT_EQ(2, active_stream->nextDecoderFilterIndexForTest());
  EXPECT_EQ(0, active_stream->nextEncoderFilterIndexForTest());

  active_stream->continueDecoding();

  EXPECT_EQ(3, active_stream->nextDecoderFilterIndexForTest());
  EXPECT_EQ(0, active_stream->nextEncoderFilterIndexForTest());
}

TEST_F(FilterTest, ActiveStreamFiltersContinueEncoding) {
  auto mock_stream_filter_0 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_1 = std::make_shared<NiceMock<MockStreamFilter>>();
  auto mock_stream_filter_2 = std::make_shared<NiceMock<MockStreamFilter>>();

  mock_stream_filters_ = {mock_stream_filter_0, mock_stream_filter_1, mock_stream_filter_2};

  auto mock_decoder_filter = std::make_shared<NiceMock<MockDecoderFilter>>();
  mock_decoder_filters_ = {mock_decoder_filter, mock_decoder_filter, mock_decoder_filter};

  ON_CALL(*mock_stream_filter_1, onStreamEncoded(_))
      .WillByDefault(Return(FilterStatus::StopIteration));

  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->newDownstreamRequest(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_EQ(6, active_stream->decoderFiltersForTest().size());
  EXPECT_EQ(3, active_stream->encoderFiltersForTest().size());

  // All decoder filters are completed directly.
  EXPECT_EQ(6, active_stream->nextDecoderFilterIndexForTest());
  EXPECT_EQ(0, active_stream->nextEncoderFilterIndexForTest());

  auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
  // `continueEncoding` will be called in the `upstreamResponse`.
  active_stream->upstreamResponse(std::move(response));

  // Encoding will be stopped when `onStreamEncoded` of `mock_stream_filter_1` is called.
  EXPECT_EQ(2, active_stream->nextEncoderFilterIndexForTest());

  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), true));

  EXPECT_CALL(*encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Response&, ResponseEncoderCallback& callback) {
        Buffer::OwnedImpl buffer;
        buffer.add("test");
        callback.onEncodingSuccess(buffer, true);
      }));

  active_stream->continueEncoding();
}

TEST_F(FilterTest, ActiveStreamSendLocalReply) {
  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->newDownstreamRequest(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  EXPECT_CALL(*creator_, response(_, _, _))
      .WillOnce(Invoke([&](Status status, absl::string_view detail, const Request&) -> ResponsePtr {
        auto response = std::make_unique<FakeStreamCodecFactory::FakeResponse>();
        response->status_ = status;
        response->status_detail_ = std::string(detail);
        return response;
      }));

  EXPECT_CALL(filter_callbacks_.connection_, write(BufferStringEqual("test"), true));

  EXPECT_CALL(*encoder_, encode(_, _))
      .WillOnce(Invoke([&](const Response& response, ResponseEncoderCallback& callback) {
        Buffer::OwnedImpl buffer;
        EXPECT_EQ(response.statusDetail(), "test_detail");
        buffer.add("test");
        callback.onEncodingSuccess(buffer, true);
      }));

  active_stream->sendLocalReply(Status::LocalExpectedError, "test_detail", [](Response&) {});
}

TEST_F(FilterTest, ActiveStreamCompleteDirectly) {
  initializeFilter();

  auto request = std::make_unique<FakeStreamCodecFactory::FakeRequest>();

  filter_->newDownstreamRequest(std::move(request));
  EXPECT_EQ(1, filter_->activeStreamsForTest().size());

  auto active_stream = filter_->activeStreamsForTest().begin()->get();

  active_stream->completeDirectly();

  EXPECT_EQ(0, filter_->activeStreamsForTest().size());
}

} // namespace
} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
