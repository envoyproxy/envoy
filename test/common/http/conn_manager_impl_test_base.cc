#include "test/common/http/conn_manager_impl_test_base.h"

#include "source/extensions/request_id/uuid/config.h"

#include "test/common/http/xff_extension.h"

using testing::AtLeast;
using testing::InSequence;
using testing::InvokeWithoutArgs;
using testing::Return;

namespace Envoy {
namespace Http {

HttpConnectionManagerImplMixin::HttpConnectionManagerImplMixin()
    : fake_stats_(*symbol_table_), http_context_(fake_stats_.symbolTable()),
      access_log_path_("dummy_path"),
      access_logs_{AccessLog::InstanceSharedPtr{new Extensions::AccessLoggers::File::FileAccessLog(
          Filesystem::FilePathAndType{Filesystem::DestinationType::File, access_log_path_}, {},
          Formatter::HttpSubstitutionFormatUtils::defaultSubstitutionFormatter(), log_manager_)}},
      codec_(new NiceMock<MockServerConnection>()),
      stats_({ALL_HTTP_CONN_MAN_STATS(POOL_COUNTER(*fake_stats_.rootScope()),
                                      POOL_GAUGE(*fake_stats_.rootScope()),
                                      POOL_HISTOGRAM(*fake_stats_.rootScope()))},
             "", *fake_stats_.rootScope()),

      listener_stats_({CONN_MAN_LISTENER_STATS(POOL_COUNTER(fake_listener_stats_))}),
      request_id_extension_(
          Extensions::RequestId::UUIDRequestIDExtension::defaultInstance(random_)),
      local_reply_(LocalReply::Factory::createDefault()) {

  ON_CALL(route_config_provider_, lastUpdated())
      .WillByDefault(Return(test_time_.timeSystem().systemTime()));
  ON_CALL(scoped_route_config_provider_, lastUpdated())
      .WillByDefault(Return(test_time_.timeSystem().systemTime()));
  // response_encoder_ is not a NiceMock on purpose. This prevents complaining about this
  // method only.
  EXPECT_CALL(response_encoder_, getStream()).Times(AtLeast(0));

  ip_detection_extensions_.push_back(getXFFExtension(0));
}

HttpConnectionManagerImplMixin::~HttpConnectionManagerImplMixin() {
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
}

Tracing::CustomTagConstSharedPtr
HttpConnectionManagerImplMixin::requestHeaderCustomTag(const std::string& header) {
  envoy::type::tracing::v3::CustomTag::Header headerTag;
  headerTag.set_name(header);
  return std::make_shared<Tracing::RequestHeaderCustomTag>(header, headerTag);
}

void HttpConnectionManagerImplMixin::setup(bool ssl, const std::string& server_name, bool tracing,
                                           bool use_srds) {
  use_srds_ = use_srds;
  if (ssl) {
    ssl_connection_ = std::make_shared<Ssl::MockConnectionInfo>();
  }

  server_name_ = server_name;
  ON_CALL(filter_callbacks_.connection_, ssl()).WillByDefault(Return(ssl_connection_));
  ON_CALL(Const(filter_callbacks_.connection_), ssl()).WillByDefault(Return(ssl_connection_));
  ON_CALL(filter_callbacks_.connection_.dispatcher_, createScaledTypedTimer_)
      .WillByDefault([&](auto, auto callback) {
        return filter_callbacks_.connection_.dispatcher_.createTimer(callback).release();
      });
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
      std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 443));
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setRemoteAddress(
      std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0"));
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setDirectRemoteAddressForTest(std::make_shared<Network::Address::Ipv4Instance>("0.0.0.0"));
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_
      ->setRequestedServerName(server_name_);
  filter_callbacks_.connection_.stream_info_.downstream_connection_info_provider_->setSslConnection(
      ssl_connection_);
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

void HttpConnectionManagerImplMixin::setupFilterChain(int num_decoder_filters,
                                                      int num_encoder_filters, int num_requests) {
  // NOTE: The length/repetition in this routine allows InSequence to work correctly in an outer
  // scope.
  for (int i = 0; i < num_decoder_filters * num_requests; i++) {
    decoder_filters_.push_back(new NiceMock<MockStreamDecoderFilter>());
  }

  for (int i = 0; i < num_encoder_filters * num_requests; i++) {
    encoder_filters_.push_back(new NiceMock<MockStreamEncoderFilter>());
  }

  InSequence s;
  for (int req = 0; req < num_requests; req++) {
    EXPECT_CALL(filter_factory_, createFilterChain(_))
        .WillOnce(Invoke([num_decoder_filters, num_encoder_filters, req,
                          this](FilterChainManager& manager) -> bool {
          bool applied_filters = false;
          if (log_handler_) {
            auto factory = createLogHandlerFactoryCb(log_handler_);
            manager.applyFilterFactoryCb({}, factory);
            applied_filters = true;
          }
          for (int i = 0; i < num_decoder_filters; i++) {
            auto factory = createDecoderFilterFactoryCb(
                StreamDecoderFilterSharedPtr{decoder_filters_[req * num_decoder_filters + i]});
            manager.applyFilterFactoryCb({}, factory);
            applied_filters = true;
          }

          for (int i = 0; i < num_encoder_filters; i++) {
            auto factory = createEncoderFilterFactoryCb(
                StreamEncoderFilterSharedPtr{encoder_filters_[req * num_encoder_filters + i]});
            manager.applyFilterFactoryCb({}, factory);
            applied_filters = true;
          }
          return applied_filters;
        }));

    for (int i = 0; i < num_decoder_filters; i++) {
      EXPECT_CALL(*decoder_filters_[req * num_decoder_filters + i], setDecoderFilterCallbacks(_));
    }

    for (int i = 0; i < num_encoder_filters; i++) {
      EXPECT_CALL(*encoder_filters_[req * num_encoder_filters + i], setEncoderFilterCallbacks(_));
    }
  }
}

void HttpConnectionManagerImplMixin::setUpBufferLimits() {
  auto& stream = response_encoder_.stream_;
  EXPECT_CALL(stream, bufferLimit()).WillOnce(Return(initial_buffer_limit_));
  EXPECT_CALL(stream, addCallbacks(_))
      .WillOnce(Invoke(
          [&](Http::StreamCallbacks& callbacks) -> void { stream_callbacks_ = &callbacks; }));
  EXPECT_CALL(stream, setFlushTimeout(_));
}

void HttpConnectionManagerImplMixin::setUpEncoderAndDecoder(bool request_with_data_and_trailers,
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

void HttpConnectionManagerImplMixin::startRequest(bool end_stream,
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

Event::MockTimer* HttpConnectionManagerImplMixin::setUpTimer() {
  // this timer belongs to whatever by whatever next creates a timer.
  // See Envoy::Event::MockTimer for details.
  return new Event::MockTimer(&filter_callbacks_.connection_.dispatcher_);
}

void HttpConnectionManagerImplMixin::sendRequestHeadersAndData() {
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

ResponseHeaderMap* HttpConnectionManagerImplMixin::sendResponseHeaders(
    ResponseHeaderMapPtr&& response_headers, absl::optional<StreamInfo::ResponseFlag> response_flag,
    std::string response_code_details) {
  ResponseHeaderMap* altered_response_headers = nullptr;

  EXPECT_CALL(*encoder_filters_[0], encodeHeaders(_, _))
      .WillOnce(Invoke([&](ResponseHeaderMap& headers, bool) -> FilterHeadersStatus {
        altered_response_headers = &headers;
        return FilterHeadersStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filters_[1], encodeHeaders(_, false))
      .WillOnce(Return(FilterHeadersStatus::Continue));
  EXPECT_CALL(response_encoder_, encodeHeaders(_, false));
  if (response_flag.has_value()) {
    decoder_filters_[0]->callbacks_->streamInfo().setResponseFlag(response_flag.value());
  }
  decoder_filters_[0]->callbacks_->streamInfo().setResponseCodeDetails("");
  decoder_filters_[0]->callbacks_->encodeHeaders(std::move(response_headers), false,
                                                 response_code_details);
  return altered_response_headers;
}

void HttpConnectionManagerImplMixin::expectOnDestroy(bool deferred) {
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

void HttpConnectionManagerImplMixin::doRemoteClose(bool deferred) {
  // We will call removeCallbacks twice.
  // Once in resetAllStreams, and once in doDeferredStreamDestroy.
  EXPECT_CALL(response_encoder_.stream_, removeCallbacks(_)).Times(2);
  expectOnDestroy(deferred);
  filter_callbacks_.connection_.raiseEvent(Network::ConnectionEvent::RemoteClose);
}

void HttpConnectionManagerImplMixin::testPathNormalization(
    const RequestHeaderMap& request_headers, const ResponseHeaderMap& expected_response) {
  setup(false, "");

  EXPECT_CALL(*codec_, dispatch(_)).WillOnce(Invoke([&](Buffer::Instance& data) -> Http::Status {
    decoder_ = &conn_manager_->newStream(response_encoder_);
    RequestHeaderMapPtr headers{std::make_unique<TestRequestHeaderMapImpl>(request_headers)};
    decoder_->decodeHeaders(std::move(headers), true);
    data.drain(4);
    return Http::okStatus();
  }));

#ifdef ENVOY_ENABLE_UHV
  expectCheckWithDefaultUhv();
#endif

  EXPECT_CALL(response_encoder_, encodeHeaders(_, true))
      .WillOnce(Invoke([&](const ResponseHeaderMap& headers, bool) -> void {
        TestResponseHeaderMapImpl copy{headers};
        copy.remove(Envoy::Http::LowerCaseString{"date"});
        copy.remove(Envoy::Http::LowerCaseString{"server"});
        EXPECT_THAT(&copy, HeaderMapEqualIgnoreOrder(&expected_response));
      }));

  Buffer::OwnedImpl fake_input("1234");
  conn_manager_->onData(fake_input, false);
}

void HttpConnectionManagerImplMixin::expectCheckWithDefaultUhv() {
  header_validator_config_.mutable_uri_path_normalization_options()->set_skip_path_normalization(
      !normalize_path_);
  header_validator_config_.mutable_uri_path_normalization_options()->set_skip_merging_slashes(
      !merge_slashes_);
  header_validator_config_.mutable_uri_path_normalization_options()
      ->set_path_with_escaped_slashes_action(
          static_cast<
              ::envoy::extensions::http::header_validators::envoy_default::v3::
                  HeaderValidatorConfig::UriPathNormalizationOptions::PathWithEscapedSlashesAction>(
              path_with_escaped_slashes_action_));
  EXPECT_CALL(header_validator_factory_, createServerHeaderValidator(codec_->protocol_, _))
      .WillOnce(InvokeWithoutArgs([this]() {
        auto header_validator = std::make_unique<
            Extensions::Http::HeaderValidators::EnvoyDefault::ServerHttp1HeaderValidator>(
            header_validator_config_, Protocol::Http11, header_validator_stats_,
            header_validator_config_overrides_);

        return header_validator;
      }));
}

void HttpConnectionManagerImplMixin::expectUhvHeaderCheck(
    HeaderValidator::ValidationResult validation_result,
    ServerHeaderValidator::RequestHeadersTransformationResult transformation_result) {
  EXPECT_CALL(header_validator_factory_, createServerHeaderValidator(codec_->protocol_, _))
      .WillOnce(InvokeWithoutArgs([validation_result, transformation_result]() {
        auto header_validator = std::make_unique<testing::StrictMock<MockServerHeaderValidator>>();
        EXPECT_CALL(*header_validator, validateRequestHeaders(_))
            .WillOnce(InvokeWithoutArgs([validation_result]() { return validation_result; }));

        if (validation_result.ok()) {
          EXPECT_CALL(*header_validator, transformRequestHeaders(_))
              .WillOnce(Invoke([transformation_result](RequestHeaderMap& headers) {
                if (transformation_result.action() ==
                    ServerHeaderValidator::RequestHeadersTransformationResult::Action::Redirect) {
                  headers.setPath("/some/new/path");
                }
                return transformation_result;
              }));
        }

        EXPECT_CALL(*header_validator, transformResponseHeaders(_))
            .WillOnce(InvokeWithoutArgs([]() {
              return ServerHeaderValidator::ResponseHeadersTransformationResult::success();
            }));

        return header_validator;
      }));
}

void HttpConnectionManagerImplMixin::expectUhvTrailerCheck(
    HeaderValidator::ValidationResult validation_result,
    HeaderValidator::TransformationResult transformation_result, bool expect_response) {
  EXPECT_CALL(header_validator_factory_, createServerHeaderValidator(codec_->protocol_, _))
      .WillOnce(InvokeWithoutArgs([validation_result, transformation_result, expect_response]() {
        auto header_validator = std::make_unique<testing::StrictMock<MockServerHeaderValidator>>();
        EXPECT_CALL(*header_validator, validateRequestHeaders(_)).WillOnce(InvokeWithoutArgs([]() {
          return HeaderValidator::ValidationResult::success();
        }));

        EXPECT_CALL(*header_validator, transformRequestHeaders(_)).WillOnce(InvokeWithoutArgs([]() {
          return ServerHeaderValidator::RequestHeadersTransformationResult::success();
        }));

        EXPECT_CALL(*header_validator, validateRequestTrailers(_))
            .WillOnce(InvokeWithoutArgs([validation_result]() { return validation_result; }));
        if (validation_result.ok()) {
          EXPECT_CALL(*header_validator, transformRequestTrailers(_))
              .WillOnce(
                  InvokeWithoutArgs([transformation_result]() { return transformation_result; }));
        }
        if (expect_response) {
          EXPECT_CALL(*header_validator, transformResponseHeaders(_))
              .WillOnce(InvokeWithoutArgs([]() {
                return ServerHeaderValidator::ResponseHeadersTransformationResult::success();
              }));
        }
        return header_validator;
      }));
}

Event::MockSchedulableCallback*
HttpConnectionManagerImplMixin::enableStreamsPerIoLimit(uint32_t limit) {
  EXPECT_CALL(runtime_.snapshot_, getInteger("http.max_requests_per_io_cycle", _))
      .WillOnce(Return(limit));

  // Expect HCM to create and set schedulable callback
  auto* deferred_request_callback =
      new Event::MockSchedulableCallback(&filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*deferred_request_callback, enabled())
      .WillRepeatedly(
          Invoke([deferred_request_callback]() { return deferred_request_callback->enabled_; }));
  EXPECT_CALL(*deferred_request_callback, scheduleCallbackNextIteration())
      .WillRepeatedly(
          Invoke([deferred_request_callback]() { deferred_request_callback->enabled_ = true; }));

  return deferred_request_callback;
}

} // namespace Http
} // namespace Envoy
