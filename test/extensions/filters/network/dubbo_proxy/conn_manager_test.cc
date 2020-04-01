#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.h"
#include "envoy/extensions/filters/network/dubbo_proxy/v3/dubbo_proxy.pb.validate.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/dubbo_proxy/app_exception.h"
#include "extensions/filters/network/dubbo_proxy/config.h"
#include "extensions/filters/network/dubbo_proxy/conn_manager.h"
#include "extensions/filters/network/dubbo_proxy/dubbo_hessian2_serializer_impl.h"
#include "extensions/filters/network/dubbo_proxy/dubbo_protocol_impl.h"
#include "extensions/filters/network/dubbo_proxy/message_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/filters/network/dubbo_proxy/mocks.h"
#include "test/extensions/filters/network/dubbo_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

using ConfigDubboProxy = envoy::extensions::filters::network::dubbo_proxy::v3::DubboProxy;

class ConnectionManagerTest;
class TestConfigImpl : public ConfigImpl {
public:
  TestConfigImpl(ConfigDubboProxy proto_config, Server::Configuration::MockFactoryContext& context,
                 DubboFilterStats& stats)
      : ConfigImpl(proto_config, context), stats_(stats) {}

  // ConfigImpl
  DubboFilterStats& stats() override { return stats_; }
  void createFilterChain(DubboFilters::FilterChainFactoryCallbacks& callbacks) override {
    if (setupChain) {
      for (auto& decoder : decoder_filters_) {
        callbacks.addDecoderFilter(decoder);
      }
      for (auto& encoder : encoder_filters_) {
        callbacks.addEncoderFilter(encoder);
      }
      return;
    }

    if (codec_filter_) {
      callbacks.addFilter(codec_filter_);
    }
  }

  void setupFilterChain(int num_decoder_filters, int num_encoder_filters) {
    for (int i = 0; i < num_decoder_filters; i++) {
      decoder_filters_.push_back(std::make_shared<NiceMock<DubboFilters::MockDecoderFilter>>());
    }
    for (int i = 0; i < num_encoder_filters; i++) {
      encoder_filters_.push_back(std::make_shared<NiceMock<DubboFilters::MockEncoderFilter>>());
    }
    setupChain = true;
  }

  void expectFilterCallbacks() {
    for (auto& decoder : decoder_filters_) {
      EXPECT_CALL(*decoder, setDecoderFilterCallbacks(_));
    }
    for (auto& encoder : encoder_filters_) {
      EXPECT_CALL(*encoder, setEncoderFilterCallbacks(_));
    }
  }

  void expectOnDestroy() {
    for (auto& decoder : decoder_filters_) {
      EXPECT_CALL(*decoder, onDestroy());
    }

    for (auto& encoder : encoder_filters_) {
      EXPECT_CALL(*encoder, onDestroy());
    }
  }

  ProtocolPtr createProtocol() override {
    if (protocol_) {
      return ProtocolPtr{protocol_};
    }
    return ConfigImpl::createProtocol();
  }

  Router::RouteConstSharedPtr route(const MessageMetadata& metadata,
                                    uint64_t random_value) const override {
    if (route_) {
      return route_;
    }
    return ConfigImpl::route(metadata, random_value);
  }

  DubboFilters::CodecFilterSharedPtr codec_filter_;
  DubboFilterStats& stats_;
  MockSerializer* serializer_{};
  MockProtocol* protocol_{};
  std::shared_ptr<Router::MockRoute> route_;

  NiceMock<DubboFilters::MockFilterChainFactory> filter_factory_;
  std::vector<std::shared_ptr<DubboFilters::MockDecoderFilter>> decoder_filters_;
  std::vector<std::shared_ptr<DubboFilters::MockEncoderFilter>> encoder_filters_;
  bool setupChain = false;
};

class ConnectionManagerTest : public testing::Test {
public:
  ConnectionManagerTest() : stats_(DubboFilterStats::generateStats("test.", store_)) {}
  ~ConnectionManagerTest() override {
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  TimeSource& timeSystem() { return factory_context_.dispatcher().timeSource(); }

  void initializeFilter() { initializeFilter(""); }

  void initializeFilter(const std::string& yaml) {
    for (const auto& counter : store_.counters()) {
      counter->reset();
    }

    if (!yaml.empty()) {
      TestUtility::loadFromYaml(yaml, proto_config_);
      TestUtility::validate(proto_config_);
    }

    proto_config_.set_stat_prefix("test");
    config_ = std::make_unique<TestConfigImpl>(proto_config_, factory_context_, stats_);
    if (custom_serializer_) {
      config_->serializer_ = custom_serializer_;
    }
    if (custom_protocol_) {
      config_->protocol_ = custom_protocol_;
    }

    ON_CALL(random_, random()).WillByDefault(Return(42));
    conn_manager_ = std::make_unique<ConnectionManager>(
        *config_, random_, filter_callbacks_.connection_.dispatcher_.timeSource());
    conn_manager_->initializeReadFilterCallbacks(filter_callbacks_);
    conn_manager_->onNewConnection();

    // NOP currently.
    conn_manager_->onAboveWriteBufferHighWatermark();
    conn_manager_->onBelowWriteBufferLowWatermark();
  }

  void writeHessianErrorResponseMessage(Buffer::Instance& buffer, bool is_event,
                                        int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x46});                     // Response status
    addInt64(buffer, request_id);                      // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x06,     // Body Length
                           '\x91',                     // return type, exception
                           0x05, 't', 'e', 's', 't'}); // return body
  }

  void writeHessianExceptionResponseMessage(Buffer::Instance& buffer, bool is_event,
                                            int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x14});
    addInt64(buffer, request_id);                      // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x06,     // Body Length
                           '\x90',                     // return type, exception
                           0x05, 't', 'e', 's', 't'}); // return body
  }

  void writeInvalidResponseMessage(Buffer::Instance& buffer) {
    buffer.add(std::string{
        '\xda', '\xbb', 0x43, 0x14, // Response Message Header, illegal serialization id
        0x00,   0x00,   0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // Request Id
        0x00,   0x00,   0x00, 0x06,                         // Body Length
        '\x94',                                             // return type
        0x05,   't',    'e',  's',  't',                    // return body
    });
  }

  void writeInvalidRequestMessage(Buffer::Instance& buffer) {
    buffer.add(std::string{
        '\xda', '\xbb', '\xc3', 0x00, // Response Message Header, illegal serialization id
        0x00,   0x00,   0x00,   0x00, 0x00, 0x00, 0x00, 0x01, // Request Id
        0x00,   0x00,   0x00,   0x16,                         // Body Length
        0x05,   '2',    '.',    '0',  '.',  '2',              // Dubbo version
        0x04,   't',    'e',    's',  't',                    // Service name
        0x05,   '0',    '.',    '0',  '.',  '0',              // Service version
        0x04,   't',    'e',    's',  't',                    // method name
    });
  }

  void writePartialHessianResponseMessage(Buffer::Instance& buffer, bool is_event,
                                          int64_t request_id, bool start) {

    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    if (start) {
      buffer.add(std::string{'\xda', '\xbb'});
      buffer.add(static_cast<void*>(&msg_type), 1);
      buffer.add(std::string{0x14});
      addInt64(buffer, request_id);                  // Request Id
      buffer.add(std::string{0x00, 0x00, 0x00, 0x06, // Body Length
                             '\x94'});               // return type, exception
    } else {
      buffer.add(std::string{0x05, 't', 'e', 's', 't'}); // return body
    }
  }

  void writeHessianResponseMessage(Buffer::Instance& buffer, bool is_event, int64_t request_id) {
    uint8_t msg_type = 0x42; // request message, two_way, not event

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x14});
    addInt64(buffer, request_id);                              // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x06,             // Body Length
                           '\x94', 0x05, 't', 'e', 's', 't'}); // return type, exception
  }

  void writePartialHessianRequestMessage(Buffer::Instance& buffer, bool is_one_way, bool is_event,
                                         int64_t request_id, bool start) {
    uint8_t msg_type = 0xc2; // request message, two_way, not event
    if (is_one_way) {
      msg_type = msg_type & 0xbf;
    }

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    if (start) {
      buffer.add(std::string{'\xda', '\xbb'});
      buffer.add(static_cast<void*>(&msg_type), 1);
      buffer.add(std::string{0x00});
      addInt64(buffer, request_id);                           // Request Id
      buffer.add(std::string{0x00, 0x00, 0x00, 0x16,          // Body Length
                             0x05, '2', '.', '0', '.', '2'}); // Dubbo version
    } else {
      buffer.add(std::string{
          0x04, 't', 'e', 's', 't',      // Service name
          0x05, '0', '.', '0', '.', '0', // Service version
          0x04, 't', 'e', 's', 't',      // method name
      });
    }
  }

  void writeHessianRequestMessage(Buffer::Instance& buffer, bool is_one_way, bool is_event,
                                  int64_t request_id) {
    uint8_t msg_type = 0xc2; // request message, two_way, not event
    if (is_one_way) {
      msg_type = msg_type & 0xbf;
    }

    if (is_event) {
      msg_type = msg_type | 0x20;
    }

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x00});
    addInt64(buffer, request_id);                            // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x16,           // Body Length
                           0x05, '2',  '.',  '0',  '.', '2', // Dubbo version
                           0x04, 't',  'e',  's',  't',      // Service name
                           0x05, '0',  '.',  '0',  '.', '0', // Service version
                           0x04, 't',  'e',  's',  't'});    // method name
  }

  void writeHessianHeartbeatRequestMessage(Buffer::Instance& buffer, int64_t request_id) {
    uint8_t msg_type = 0xc2; // request message, two_way, not event
    msg_type = msg_type | 0x20;

    buffer.add(std::string{'\xda', '\xbb'});
    buffer.add(static_cast<void*>(&msg_type), 1);
    buffer.add(std::string{0x14});
    addInt64(buffer, request_id);                    // Request Id
    buffer.add(std::string{0x00, 0x00, 0x00, 0x01}); // Body Length
    buffer.add(std::string{0x01});                   // Body
  }

  NiceMock<Server::Configuration::MockFactoryContext> factory_context_;
  Stats::TestUtil::TestStore store_;
  DubboFilterStats stats_;
  ConfigDubboProxy proto_config_;

  std::unique_ptr<TestConfigImpl> config_;

  Buffer::OwnedImpl buffer_;
  Buffer::OwnedImpl write_buffer_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Runtime::MockRandomGenerator> random_;
  std::unique_ptr<ConnectionManager> conn_manager_;
  MockSerializer* custom_serializer_{};
  MockProtocol* custom_protocol_{};
};

TEST_F(ConnectionManagerTest, OnDataHandlesRequestTwoWay) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(0U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_event").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(ConnectionManagerTest, OnDataHandlesRequestOneWay) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, true, false, 0x0F);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(1U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_event").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
  EXPECT_EQ(0U, store_.counter("test.response").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, OnDataHandlesHeartbeatEvent) {
  initializeFilter();
  writeHessianHeartbeatRequestMessage(buffer_, 0x0F);

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        ProtocolPtr protocol = conn_manager_->config().createProtocol();
        MessageMetadataSharedPtr metadata(std::make_shared<MessageMetadata>());
        auto result = protocol->decodeHeader(buffer, metadata);
        EXPECT_TRUE(result.second);
        const DubboProxy::ContextImpl& ctx = *static_cast<const ContextImpl*>(result.first.get());
        EXPECT_TRUE(ctx.is_heartbeat());
        EXPECT_TRUE(metadata->hasResponseStatus());
        EXPECT_FALSE(metadata->is_two_way());
        EXPECT_EQ(ProtocolType::Dubbo, metadata->protocol_type());
        EXPECT_EQ(metadata->response_status(), ResponseStatus::Ok);
        EXPECT_EQ(metadata->message_type(), MessageType::HeartbeatResponse);
        buffer.drain(ctx.header_size());
      }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, buffer_.length());
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_event").value());
}

TEST_F(ConnectionManagerTest, HandlesHeartbeatWithException) {
  custom_protocol_ = new NiceMock<MockProtocol>();
  initializeFilter();

  EXPECT_CALL(*custom_protocol_, encode(_, _, _, _)).WillOnce(Return(false));

  MessageMetadataSharedPtr meta = std::make_shared<MessageMetadata>();
  EXPECT_THROW_WITH_MESSAGE(conn_manager_->onHeartbeat(meta), EnvoyException,
                            "failed to encode heartbeat message");
}

TEST_F(ConnectionManagerTest, OnDataHandlesMessageSplitAcrossBuffers) {
  initializeFilter();
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, true);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0, buffer_.length());

  // Complete the buffer
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, false);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1U, store_.counter("test.request_twoway").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
}

TEST_F(ConnectionManagerTest, OnDataHandlesProtocolError) {
  initializeFilter();
  writeInvalidRequestMessage(buffer_);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0, buffer_.length());

  // Sniffing is now disabled.
  bool one_way = true;
  writeHessianRequestMessage(buffer_, one_way, false, 0x0F);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
}

TEST_F(ConnectionManagerTest, OnDataHandlesProtocolErrorOnWrite) {
  initializeFilter();
  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  // Start the read buffer
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, true);
  uint64_t len = buffer_.length();

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  len -= buffer_.length();

  // Disable sniffing
  writeInvalidRequestMessage(write_buffer_);

  callbacks->startUpstreamResponse();

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_NE(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
}

TEST_F(ConnectionManagerTest, OnDataStopsSniffingWithTooManyPendingCalls) {
  initializeFilter();
  config_->setupFilterChain(1, 0);
  // config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  int request_count = 64;
  for (int i = 0; i < request_count; i++) {
    writeHessianRequestMessage(buffer_, false, false, i);
  }

  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_)).Times(request_count);
  EXPECT_CALL(*decoder_filter, onDestroy()).Times(request_count);
  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _)).Times(request_count);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(64U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  // Sniffing is now disabled.
  writeInvalidRequestMessage(buffer_);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, OnWriteHandlesResponse) {
  uint64_t request_id = 100;
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, request_id);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writeHessianResponseMessage(write_buffer_, false, request_id);

  callbacks->startUpstreamResponse();

  EXPECT_EQ(callbacks->requestId(), request_id);
  EXPECT_EQ(callbacks->connection(), &(filter_callbacks_.connection_));
  EXPECT_GE(callbacks->streamId(), 0);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, HandlesResponseContainExceptionInfo) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_decoding_success").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writeHessianExceptionResponseMessage(write_buffer_, false, 1);

  callbacks->startUpstreamResponse();

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_decoding_success").value());
  EXPECT_EQ(1U, store_.counter("test.response_business_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, HandlesResponseError) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writeHessianErrorResponseMessage(write_buffer_, false, 1);

  callbacks->startUpstreamResponse();

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(1U, store_.counter("test.response_error").value());
  EXPECT_EQ(0U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, OnWriteHandlesResponseException) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());

  writeInvalidRequestMessage(write_buffer_);

  callbacks->startUpstreamResponse();

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Reset, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(1U, store_.counter("test.local_response_business_exception").value());
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
}

// Tests stop iteration/resume with multiple filters.
TEST_F(ConnectionManagerTest, OnDataResumesWithNextFilter) {
  initializeFilter();

  config_->setupFilterChain(2, 0);
  config_->expectOnDestroy();
  auto first_filter = config_->decoder_filters_[0];
  auto second_filter = config_->decoder_filters_[1];

  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*first_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*second_filter, setDecoderFilterCallbacks(_));

  // First filter stops iteration.
  {
    EXPECT_CALL(*first_filter, onMessageDecoded(_, _))
        .WillOnce(Return(FilterStatus::StopIteration));
    EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
    EXPECT_EQ(0U, store_.counter("test.request").value());
    EXPECT_EQ(1U,
              store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
  }

  // Resume processing.
  {
    InSequence s;
    EXPECT_CALL(*first_filter, onMessageDecoded(_, _)).WillOnce(Return(FilterStatus::Continue));
    EXPECT_CALL(*second_filter, onMessageDecoded(_, _)).WillOnce(Return(FilterStatus::Continue));
    callbacks->continueDecoding();
  }

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

// Tests multiple filters are invoked in the correct order.
TEST_F(ConnectionManagerTest, OnDataHandlesDubboCallWithMultipleFilters) {
  initializeFilter();

  config_->setupFilterChain(2, 0);
  config_->expectOnDestroy();
  auto first_filter = config_->decoder_filters_[0];
  auto second_filter = config_->decoder_filters_[1];

  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  InSequence s;
  EXPECT_CALL(*first_filter, onMessageDecoded(_, _)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*second_filter, onMessageDecoded(_, _)).WillOnce(Return(FilterStatus::Continue));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, PipelinedRequestAndResponse) {
  initializeFilter();

  config_->setupFilterChain(1, 0);
  auto decoder_filter = config_->decoder_filters_[0];

  writeHessianRequestMessage(buffer_, false, false, 1);
  writeHessianRequestMessage(buffer_, false, false, 2);

  std::list<DubboFilters::DecoderFilterCallbacks*> callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillRepeatedly(Invoke(
          [&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks.push_back(&cb); }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(2U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
  EXPECT_EQ(2U, store_.counter("test.request").value());

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(2);
  EXPECT_CALL(*decoder_filter, onDestroy()).Times(2);

  writeHessianResponseMessage(write_buffer_, false, 0x01);
  callbacks.front()->startUpstreamResponse();
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete,
            callbacks.front()->upstreamData(write_buffer_));
  callbacks.pop_front();
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());

  writeHessianResponseMessage(write_buffer_, false, 0x02);
  callbacks.front()->startUpstreamResponse();
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete,
            callbacks.front()->upstreamData(write_buffer_));
  callbacks.pop_front();
  EXPECT_EQ(2U, store_.counter("test.response").value());
  EXPECT_EQ(2U, store_.counter("test.response_success").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, ResetDownstreamConnection) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  callbacks->resetDownstreamConnection();

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, OnEvent) {
  // No active calls
  {
    initializeFilter();
    conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
    conn_manager_->onEvent(Network::ConnectionEvent::LocalClose);
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_local_with_active_rq").value());
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
  }

  // Remote close mid-request
  {
    initializeFilter();

    writePartialHessianRequestMessage(buffer_, false, false, 1, true);
    EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
  }

  // Local close mid-request
  {
    initializeFilter();
    writePartialHessianRequestMessage(buffer_, false, false, 1, true);
    EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    conn_manager_->onEvent(Network::ConnectionEvent::LocalClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    buffer_.drain(buffer_.length());
  }

  // Remote close before response
  {
    initializeFilter();
    writeHessianRequestMessage(buffer_, false, false, 1);
    EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    conn_manager_->onEvent(Network::ConnectionEvent::RemoteClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

    buffer_.drain(buffer_.length());
  }

  // Local close before response
  {
    initializeFilter();
    writeHessianRequestMessage(buffer_, false, false, 1);
    EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    conn_manager_->onEvent(Network::ConnectionEvent::LocalClose);
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    buffer_.drain(buffer_.length());
  }
}
TEST_F(ConnectionManagerTest, ResponseWithUnknownSequenceID) {
  initializeFilter();

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  writeHessianRequestMessage(buffer_, false, false, 1);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  writeHessianResponseMessage(write_buffer_, false, 10);

  callbacks->startUpstreamResponse();

  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Reset, callbacks->upstreamData(write_buffer_));
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
}

TEST_F(ConnectionManagerTest, OnDataWithFilterSendsLocalReply) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  config_->setupFilterChain(2, 0);
  config_->expectOnDestroy();
  auto& first_filter = config_->decoder_filters_[0];
  auto& second_filter = config_->decoder_filters_[1];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*first_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*second_filter, setDecoderFilterCallbacks(_));

  const std::string fake_response("mock dubbo response");
  NiceMock<DubboFilters::MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _))
      .WillOnce(Invoke([&](MessageMetadata&, Protocol&,
                           Buffer::Instance& buffer) -> DubboFilters::DirectResponse::ResponseType {
        buffer.add(fake_response);
        return DubboFilters::DirectResponse::ResponseType::SuccessReply;
      }));

  // First filter sends local reply.
  EXPECT_CALL(*first_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        callbacks->streamInfo().setResponseFlag(StreamInfo::ResponseFlag::NoRouteFound);
        callbacks->sendLocalReply(direct_response, false);
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(fake_response, buffer.toString());
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(SerializationType::Hessian2, callbacks->serializationType());
  EXPECT_EQ(ProtocolType::Dubbo, callbacks->protocolType());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.local_response_success").value());
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, OnDataWithFilterSendsLocalErrorReply) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 1);

  config_->setupFilterChain(2, 0);
  config_->expectOnDestroy();
  auto& first_filter = config_->decoder_filters_[0];
  auto& second_filter = config_->decoder_filters_[1];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*first_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*second_filter, setDecoderFilterCallbacks(_));

  const std::string fake_response("mock dubbo response");
  NiceMock<DubboFilters::MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _))
      .WillOnce(Invoke([&](MessageMetadata&, Protocol&,
                           Buffer::Instance& buffer) -> DubboFilters::DirectResponse::ResponseType {
        buffer.add(fake_response);
        return DubboFilters::DirectResponse::ResponseType::ErrorReply;
      }));

  // First filter sends local reply.
  EXPECT_CALL(*first_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        callbacks->sendLocalReply(direct_response, false);
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(fake_response, buffer.toString());
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.local_response_error").value());
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, TwoWayRequestWithEndStream) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        return FilterStatus::StopIteration;
      }));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite))
      .Times(1);
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(conn_manager_->onData(buffer_, true), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
}

TEST_F(ConnectionManagerTest, OneWayRequestWithEndStream) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, true, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite))
      .Times(1);
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(conn_manager_->onData(buffer_, true), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
}

TEST_F(ConnectionManagerTest, EmptyRequestData) {
  initializeFilter();
  buffer_.drain(buffer_.length());

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(0);
  EXPECT_EQ(conn_manager_->onData(buffer_, true), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, StopHandleRequest) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  ON_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillByDefault(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        return FilterStatus::StopIteration;
      }));

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite))
      .Times(0);
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(0);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
}

TEST_F(ConnectionManagerTest, HandlesHeartbeatEventWithConnectionClose) {
  initializeFilter();
  writeHessianHeartbeatRequestMessage(buffer_, 0x0F);

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false)).Times(0);

  filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_event").value());
}

TEST_F(ConnectionManagerTest, SendsLocalReplyWithCloseConnection) {
  initializeFilter();

  const std::string fake_response("mock dubbo response");
  NiceMock<DubboFilters::MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _))
      .WillOnce(Invoke([&](MessageMetadata&, Protocol&,
                           Buffer::Instance& buffer) -> DubboFilters::DirectResponse::ResponseType {
        buffer.add(fake_response);
        return DubboFilters::DirectResponse::ResponseType::ErrorReply;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite))
      .Times(1);

  MessageMetadata metadata;
  conn_manager_->sendLocalReply(metadata, direct_response, true);
  EXPECT_EQ(1U, store_.counter("test.local_response_error").value());

  // The connection closed.
  EXPECT_CALL(direct_response, encode(_, _, _)).Times(0);
  conn_manager_->sendLocalReply(metadata, direct_response, true);
}

TEST_F(ConnectionManagerTest, ContinueDecodingWithHalfClose) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, true, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite))
      .Times(1);
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(conn_manager_->onData(buffer_, true), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

  conn_manager_->continueDecoding();
}

TEST_F(ConnectionManagerTest, RoutingSuccess) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  config_->route_ = std::make_shared<Router::MockRoute>();
  EXPECT_EQ(config_->route_, callbacks->route());

  // Use the cache.
  EXPECT_NE(nullptr, callbacks->route());
}

TEST_F(ConnectionManagerTest, RoutingFailure) {
  initializeFilter();
  writePartialHessianRequestMessage(buffer_, false, false, 0x0F, true);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _)).Times(0);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  // The metadata is nullptr.
  config_->route_ = std::make_shared<Router::MockRoute>();
  EXPECT_EQ(nullptr, callbacks->route());
}

TEST_F(ConnectionManagerTest, ResetStream) {
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, 0x0F);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  callbacks->resetStream();
}

TEST_F(ConnectionManagerTest, NeedMoreDataForHandleResponse) {
  uint64_t request_id = 100;
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, request_id);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writePartialHessianRequestMessage(write_buffer_, false, false, 0x0F, true);

  callbacks->startUpstreamResponse();

  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::MoreData, callbacks->upstreamData(write_buffer_));
}

TEST_F(ConnectionManagerTest, PendingMessageEnd) {
  uint64_t request_id = 100;
  initializeFilter();
  writeHessianRequestMessage(buffer_, false, false, request_id);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        return FilterStatus::StopIteration;
      }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());
}

TEST_F(ConnectionManagerTest, Routing) {
  const std::string yaml = R"EOF(
stat_prefix: test
protocol_type: Dubbo
serialization_type: Hessian2
route_config:
  - name: test1
    interface: org.apache.dubbo.demo.DemoService
    routes:
      - match:
          method:
            name:
              safe_regex:
                google_re2: {}
                regex: "(.*?)"
        route:
            cluster: user_service_dubbo_server
)EOF";

  initializeFilter(yaml);
  writeHessianRequestMessage(buffer_, false, false, 100);

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr) -> FilterStatus {
        auto invo = static_cast<const RpcInvocationBase*>(&metadata->invocation_info());
        auto data = const_cast<RpcInvocationBase*>(invo);
        data->setServiceName("org.apache.dubbo.demo.DemoService");
        data->setMethodName("test");
        return FilterStatus::StopIteration;
      }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  Router::RouteConstSharedPtr route = callbacks->route();
  EXPECT_NE(nullptr, route);
  EXPECT_NE(nullptr, route->routeEntry());
  EXPECT_EQ("user_service_dubbo_server", route->routeEntry()->clusterName());
}

TEST_F(ConnectionManagerTest, TransportEndWithConnectionClose) {
  initializeFilter();

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  writeHessianRequestMessage(buffer_, false, false, 1);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  writeHessianResponseMessage(write_buffer_, false, 1);

  callbacks->startUpstreamResponse();

  filter_callbacks_.connection_.close(Network::ConnectionCloseType::FlushWrite);

  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Reset, callbacks->upstreamData(write_buffer_));
  EXPECT_EQ(1U, store_.counter("test.response_error_caused_connection_close").value());
}

TEST_F(ConnectionManagerTest, MessageDecodedReturnStopIteration) {
  initializeFilter();

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  // The sendLocalReply is not called and the message type is not oneway,
  // the ActiveMessage object is not destroyed.
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(0);

  writeHessianRequestMessage(buffer_, false, false, 1);

  size_t buf_size = buffer_.length();
  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr ctx) -> FilterStatus {
        EXPECT_EQ(ctx->message_size(), buf_size);
        return FilterStatus::StopIteration;
      }));

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  // Buffer data should be consumed.
  EXPECT_EQ(0, buffer_.length());

  // The finalizeRequest should not be called.
  EXPECT_EQ(0U, store_.counter("test.request").value());
}

TEST_F(ConnectionManagerTest, SendLocalReplyInMessageDecoded) {
  initializeFilter();

  config_->setupFilterChain(1, 0);
  config_->expectOnDestroy();
  auto& decoder_filter = config_->decoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  const std::string fake_response("mock dubbo response");
  NiceMock<DubboFilters::MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _))
      .WillOnce(Invoke([&](MessageMetadata&, Protocol&,
                           Buffer::Instance& buffer) -> DubboFilters::DirectResponse::ResponseType {
        buffer.add(fake_response);
        return DubboFilters::DirectResponse::ResponseType::ErrorReply;
      }));
  EXPECT_CALL(*decoder_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr, ContextSharedPtr) -> FilterStatus {
        EXPECT_EQ(1, conn_manager_->getActiveMessagesForTest().size());
        EXPECT_NE(nullptr, conn_manager_->getActiveMessagesForTest().front()->metadata());
        callbacks->sendLocalReply(direct_response, false);
        return FilterStatus::StopIteration;
      }));

  // The sendLocalReply is called, the ActiveMessage object should be destroyed.
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);

  writeHessianRequestMessage(buffer_, false, false, 1);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  // Buffer data should be consumed.
  EXPECT_EQ(0, buffer_.length());

  // The finalizeRequest should be called.
  EXPECT_EQ(1U, store_.counter("test.request").value());
}

TEST_F(ConnectionManagerTest, HandleResponseWithEncoderFilter) {
  uint64_t request_id = 100;
  initializeFilter();

  writeHessianRequestMessage(buffer_, false, false, request_id);

  config_->setupFilterChain(1, 1);
  auto& decoder_filter = config_->decoder_filters_[0];
  auto& encoder_filter = config_->encoder_filters_[0];

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_CALL(*encoder_filter, setEncoderFilterCallbacks(_)).Times(1);

  EXPECT_CALL(*decoder_filter, onDestroy()).Times(1);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writeHessianResponseMessage(write_buffer_, false, request_id);

  callbacks->startUpstreamResponse();

  EXPECT_EQ(callbacks->requestId(), request_id);
  EXPECT_EQ(callbacks->connection(), &(filter_callbacks_.connection_));
  EXPECT_GE(callbacks->streamId(), 0);

  size_t expect_response_length = write_buffer_.length();
  EXPECT_CALL(*encoder_filter, onMessageEncoded(_, _))
      .WillOnce(
          Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) -> FilterStatus {
            EXPECT_EQ(metadata->request_id(), request_id);
            EXPECT_EQ(ctx->message_size(), expect_response_length);
            return FilterStatus::Continue;
          }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));
  EXPECT_CALL(*encoder_filter, onDestroy()).Times(1);
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
}

TEST_F(ConnectionManagerTest, HandleResponseWithCodecFilter) {
  uint64_t request_id = 100;
  initializeFilter();
  config_->codec_filter_ = std::make_unique<DubboFilters::MockCodecFilter>();
  auto mock_codec_filter =
      static_cast<DubboFilters::MockCodecFilter*>(config_->codec_filter_.get());

  writeHessianRequestMessage(buffer_, false, false, request_id);

  DubboFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*mock_codec_filter, setDecoderFilterCallbacks(_))
      .WillOnce(Invoke([&](DubboFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*mock_codec_filter, onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr) -> FilterStatus {
        EXPECT_EQ(metadata->request_id(), request_id);
        return FilterStatus::Continue;
      }));

  EXPECT_CALL(*mock_codec_filter, setEncoderFilterCallbacks(_)).Times(1);

  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active", Stats::Gauge::ImportMode::Accumulate).value());

  writeHessianResponseMessage(write_buffer_, false, request_id);

  callbacks->startUpstreamResponse();

  EXPECT_EQ(callbacks->requestId(), request_id);
  EXPECT_EQ(callbacks->connection(), &(filter_callbacks_.connection_));
  EXPECT_GE(callbacks->streamId(), 0);

  size_t expect_response_length = write_buffer_.length();
  EXPECT_CALL(*mock_codec_filter, onMessageEncoded(_, _))
      .WillOnce(
          Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr ctx) -> FilterStatus {
            EXPECT_EQ(metadata->request_id(), request_id);
            EXPECT_EQ(ctx->message_size(), expect_response_length);
            return FilterStatus::Continue;
          }));

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete, callbacks->upstreamData(write_buffer_));
  EXPECT_CALL(*mock_codec_filter, onDestroy()).Times(1);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
}

TEST_F(ConnectionManagerTest, AddDataWithStopAndContinue) {
  InSequence s;
  initializeFilter();
  config_->setupFilterChain(3, 3);

  uint64_t request_id = 100;

  EXPECT_CALL(*config_->decoder_filters_[0], onMessageDecoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr) -> FilterStatus {
        EXPECT_EQ(metadata->request_id(), request_id);
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*config_->decoder_filters_[1], onMessageDecoded(_, _))
      .WillOnce(Return(FilterStatus::StopIteration))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*config_->decoder_filters_[2], onMessageDecoded(_, _))
      .WillOnce(Return(FilterStatus::Continue));
  writeHessianRequestMessage(buffer_, false, false, request_id);
  EXPECT_EQ(conn_manager_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  config_->decoder_filters_[1]->callbacks_->continueDecoding();

  // For encode direction
  EXPECT_CALL(*config_->encoder_filters_[0], onMessageEncoded(_, _))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata, ContextSharedPtr) -> FilterStatus {
        EXPECT_EQ(metadata->request_id(), request_id);
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*config_->encoder_filters_[1], onMessageEncoded(_, _))
      .WillOnce(Return(FilterStatus::StopIteration))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*config_->encoder_filters_[2], onMessageEncoded(_, _))
      .WillOnce(Return(FilterStatus::Continue));

  writeHessianResponseMessage(write_buffer_, false, request_id);
  config_->decoder_filters_[0]->callbacks_->startUpstreamResponse();
  EXPECT_EQ(DubboFilters::UpstreamResponseStatus::Complete,
            config_->decoder_filters_[0]->callbacks_->upstreamData(write_buffer_));

  config_->encoder_filters_[1]->callbacks_->continueEncoding();
  config_->expectOnDestroy();
}

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
