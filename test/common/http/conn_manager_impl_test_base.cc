#include "test/common/http/conn_manager_impl_test_base.h"

using testing::AtLeast;
using testing::InSequence;
using testing::InvokeWithoutArgs;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Http {

HttpConnectionManagerImplTest::HttpConnectionManagerImplTest()
    : http_context_(fake_stats_.symbolTable()), access_log_path_("dummy_path"),
      access_logs_{AccessLog::InstanceSharedPtr{new Extensions::AccessLoggers::File::FileAccessLog(
          access_log_path_, {}, Formatter::SubstitutionFormatUtils::defaultSubstitutionFormatter(),
          log_manager_)}},
      codec_(new NiceMock<MockServerConnection>()),
      stats_({ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER(fake_stats_), POOL_GAUGE(fake_stats_),
                                      POOL_HISTOGRAM(fake_stats_))},
             "", fake_stats_),

      listener_stats_({CONN_MAN_LISTENER_STATS(POOL_COUNTER(fake_listener_stats_))}),
      request_id_extension_(RequestIDExtensionFactory::defaultInstance(random_)),
      local_reply_(LocalReply::Factory::createDefault()) {

  ON_CALL(route_config_provider_, lastUpdated())
      .WillByDefault(Return(test_time_.timeSystem().systemTime()));
  ON_CALL(scoped_route_config_provider_, lastUpdated())
      .WillByDefault(Return(test_time_.timeSystem().systemTime()));
  // response_encoder_ is not a NiceMock on purpose. This prevents complaining about this
  // method only.
  EXPECT_CALL(response_encoder_, getStream()).Times(AtLeast(0));
}

HttpConnectionManagerImplTest::~HttpConnectionManagerImplTest() {
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
}

Tracing::CustomTagConstSharedPtr
HttpConnectionManagerImplTest::requestHeaderCustomTag(const std::string& header) {
  envoy::type::tracing::v3::CustomTag::Header headerTag;
  headerTag.set_name(header);
  return std::make_shared<Tracing::RequestHeaderCustomTag>(header, headerTag);
}

void HttpConnectionManagerImplTest::setup(bool ssl, const std::string& server_name, bool tracing,
                                          bool use_srds) {
  use_srds_ = use_srds;
  if (ssl) {
    ssl_connection_ = std::make_shared<Ssl::MockConnectionInfo>();
  }

  server_name_ = server_name;
  ON_CALL(filter_callbacks_.connection_, ssl()).WillByDefault(Return(ssl_connection_));
  ON_CALL(Const(filter_callbacks_.connection_), ssl()).WillByDefault(Return(ssl_connection_));
  filter_callbacks_.connection_.local_address_ =
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 443);
  filter_callbacks_.connection_.remote_address_ =
      std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0");
  conn_manager_ = std::make_unique<ConnectionManagerImpl>(
      *this, drain_close_, random_, http_context_, runtime_, local_info_, cluster_manager_,
      overload_manager_, test_time_.timeSystem());
  conn_manager_->initializeReadFilterCallbacks(filter_callbacks_);

  if (tracing) {
    envoy::type::v3::FractionalPercent percent1;
    percent1.set_numerator(100);
    envoy::type::v3::FractionalPercent percent2;
    percent2.set_numerator(10000);
    percent2.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
    tracing_config_ = std::make_unique<TracingConnectionManagerConfig>(
        TracingConnectionManagerConfig{Tracing::OperationName::Ingress,
                                       {{":method", requestHeaderCustomTag(":method")}},
                                       percent1,
                                       percent2,
                                       percent1,
                                       false,
                                       256});
  }
}

void HttpConnectionManagerImplTest::setupFilterChain(int num_decoder_filters,
                                                     int num_encoder_filters, int num_requests) {
  // NOTE: The length/repetition in this routine allows InSequence to work correctly in an outer
  // scope.
  for (int i = 0; i < num_decoder_filters * num_requests; i++) {
    decoder_filters_.push_back(new MockStreamDecoderFilter());
  }

  for (int i = 0; i < num_encoder_filters * num_requests; i++) {
    encoder_filters_.push_back(new MockStreamEncoderFilter());
  }

  InSequence s;
  for (int req = 0; req < num_requests; req++) {
    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillOnce(Invoke([num_decoder_filters, num_encoder_filters, req,
                          this](FilterChainFactoryCallbacks& callbacks) -> void {
          if (log_handler_.get()) {
            callbacks.addAccessLogHandler(log_handler_);
          }
          for (int i = 0; i < num_decoder_filters; i++) {
            callbacks.addStreamDecoderFilter(
                StreamDecoderFilterSharedPtr{decoder_filters_[req * num_decoder_filters + i]});
          }

          for (int i = 0; i < num_encoder_filters; i++) {
            callbacks.addStreamEncoderFilter(
                StreamEncoderFilterSharedPtr{encoder_filters_[req * num_encoder_filters + i]});
          }
        }));

    for (int i = 0; i < num_decoder_filters; i++) {
      EXPECT_CALL(*decoder_filters_[req * num_decoder_filters + i], setDecoderFilterCallbacks(_));
    }

    for (int i = 0; i < num_encoder_filters; i++) {
      EXPECT_CALL(*encoder_filters_[req * num_encoder_filters + i], setEncoderFilterCallbacks(_));
    }
  }
}

void HttpConnectionManagerImplTest::setUpBufferLimits() {
  ON_CALL(response_encoder_, getStream()).WillByDefault(ReturnRef(stream_));
  EXPECT_CALL(stream_, bufferLimit()).WillOnce(Return(initial_buffer_limit_));
  EXPECT_CALL(stream_, addCallbacks(_))
      .WillOnce(Invoke(
          [&](Http::StreamCallbacks& callbacks) -> void { stream_callbacks_ = &callbacks; }));
  EXPECT_CALL(stream_, setFlushTimeout(_));
}

void HttpConnectionManagerImplTest::setUpEncoderAndDecoder(bool request_with_data_and_trailers,
                                                           bool decode_headers_stop_all) {
  setUpBufferLimits();
  EXPECT_CALL(*codec_, dispatch(_))
      .WillOnce(Invoke([&, request_with_data_and_trailers](Buffer::Instance&) -> Http::Status {
        RequestDecoder* decoder = &conn_manager_->newStream(response_encoder_);
        RequestHeaderMapPtr headers{new TestRequestHeaderMapImpl{
            {":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
        if (request_with_data_and_trailers) {
          decoder->decodeHeaders(std::move(headers), false);

          Buffer::OwnedImpl fake_data("12345");
          decoder->decodeData(fake_data, false);

          RequestTrailerMapPtr trailers{new TestRequestTrailerMapImpl{{"foo", "bar"}}};
          decoder->decodeTrailers(std::move(trailers));
        } else {
          decoder->decodeHeaders(std::move(headers), true);
        }
        return Http::okStatus();
      }));

  setupFilterChain(2, 2);

  EXPECT_CALL(*decoder_filters_[0], decodeHeaders(_, _))
      .WillOnce(InvokeWithoutArgs([&, decode_headers_stop_all]() -> FilterHeadersStatus {
        Buffer::OwnedImpl data("hello");
        decoder_filters_[0]->callbacks_->addDecodedData(data, true);
        if (decode_headers_stop_all) {
          return FilterHeadersStatus::StopAllIterationAndBuffer;
        } else {
          return FilterHeadersStatus::Continue;
        }
      }));
  EXPECT_CALL(*decoder_filters_[0], decodeComplete());
}

void HttpConnectionManagerImplTest::startRequest(bool end_stream,
                                                 absl::optional<std::string> body) {
  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance&) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{
        new TestRequestHeaderMapImpl{{":authority", "host"}, {":path", "/"}, {":method", "GET"}}};
    decoder_->decodeHeaders(std::move(headers), end_stream && !body.has_value());
    if (body.has_value()) {
      Buffer::OwnedImpl fake_data(body.value());
      decoder_->decodeData(fake_data, end_stream);
    }
    return Http::okStatus();
  }));
  Buffer::OwnedImpl fake_input;
  conn_manager_->onData(fake_input, false);
}

Event::MockTimer* HttpConnectionManagerImplTest::setUpTimer() {
  // this timer belongs to whatever by whatever next creates a timer.
  // See Envoy::Event::MockTimer for details.
  return new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
}

void HttpConnectionManagerImplTest::sendRequestHeadersAndData() {
  EXPECT_CALL(*decoder_filters_[1], decodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::StopIteration));
  auto status = streaming_filter_ ? FilterDataStatus::StopIterationAndWatermark
                                  : FilterDataStatus::StopIterationAndBuffer;
  EXPECT_CALL(*decoder_filters_[1], decodeData(_, true)).WillOnce(Return(status));
  EXPECT_CALL(*decoder_filters_[1], decodeComplete());

  // Kick off the incoming data. |fake_input| is not sent, but instead kicks
  // off sending the headers and |data| queued up in setUpEncoderAndDecoder().
  Buffer::OwnedImpl fake_input("asdf");
  conn_manager_->onData(fake_input, false);
}

ResponseHeaderMap*
HttpConnectionManagerImplTest::sendResponseHeaders(ResponseHeaderMapPtr&& response_headers) {
  ResponseHeaderMap* altered_response_headers = nullptr;

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, _))
      .WillOnce(Invoke([&](ResponseHeaderMap& headers, bool) -> FilterHeadersStatus {
        altered_response_headers = &headers;
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false, "details");
  return altered_response_headers;
}

void HttpConnectionManagerImplTest::expectOnDestroy(bool deferred) {
  for (auto filter : decoder_filters_) {
    EXPECT_CALL(*filter, onStreamComplete());
  }
  {
    auto setup_filter_expect = [](MockStreamEncoderFilter* filter) {
      EXPECT_CALL(*filter, onStreamComplete());
    };
    std::for_each(encoder_filters_.rbegin(), encoder_filters_.rend(), setup_filter_expect);
  }

  for (auto filter : decoder_filters_) {
    EXPECT_CALL(*filter, onDestroy());
  }
  {
    auto setup_filter_expect = [](MockStreamEncoderFilter* filter) {
      EXPECT_CALL(*filter, onDestroy());
    };
    std::for_each(encoder_filters_.rbegin(), encoder_filters_.rend(), setup_filter_expect);
  }

  if (deferred) {
    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  }
}

void HttpConnectionManagerImplTest::doRemoteClose(bool deferred) {
  EXPECT_CALL(stream_, removeCallbacks(_));
  expectOnDestroy(deferred);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

} // namespace Http
} // namespace Envoy
