#include "envoy/config/filter/network/thrift_proxy/v2alpha1/thrift_proxy.pb.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/buffer_helper.h"
#include "extensions/filters/network/thrift_proxy/config.h"
#include "extensions/filters/network/thrift_proxy/conn_manager.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;
using testing::_;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class TestConfigImpl : public ConfigImpl {
public:
  TestConfigImpl(envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy proto_config,
                 Server::Configuration::MockFactoryContext& context,
                 ThriftFilters::DecoderFilterSharedPtr decoder_filter, ThriftFilterStats& stats)
      : ConfigImpl(proto_config, context), decoder_filter_(decoder_filter), stats_(stats) {}

  // ConfigImpl
  ThriftFilterStats& stats() override { return stats_; }
  void createFilterChain(ThriftFilters::FilterChainFactoryCallbacks& callbacks) override {
    callbacks.addDecoderFilter(decoder_filter_);
  }

private:
  ThriftFilters::DecoderFilterSharedPtr decoder_filter_;
  ThriftFilterStats& stats_;
};

class ThriftConnectionManagerTest : public testing::Test {
public:
  ThriftConnectionManagerTest() : stats_(ThriftFilterStats::generateStats("test.", store_)) {}
  ~ThriftConnectionManagerTest() {
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  void initializeFilter() { initializeFilter(""); }

  void initializeFilter(const std::string& yaml) {
    // Destroy any existing filter first.
    filter_ = nullptr;

    for (auto counter : store_.counters()) {
      counter->reset();
    }

    if (yaml.empty()) {
      proto_config_.set_stat_prefix("test");
    } else {
      MessageUtil::loadFromYaml(yaml, proto_config_);
      MessageUtil::validate(proto_config_);
    }

    proto_config_.set_stat_prefix("test");

    decoder_filter_.reset(new NiceMock<ThriftFilters::MockDecoderFilter>());
    config_.reset(new TestConfigImpl(proto_config_, context_, decoder_filter_, stats_));

    filter_.reset(new ConnectionManager(*config_));
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    filter_->onNewConnection();

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  void writeMessage(Buffer::Instance& buffer, TransportType transport_type,
                    ProtocolType protocol_type, MessageType msg_type, int32_t seq_id) {
    Buffer::OwnedImpl msg;
    ProtocolPtr proto = NamedProtocolConfigFactory::getFactory(protocol_type).createProtocol();
    MessageMetadata metadata;
    metadata.setProtocol(protocol_type);
    metadata.setMethodName("name");
    metadata.setMessageType(msg_type);
    metadata.setSequenceId(seq_id);

    proto->writeMessageBegin(msg, metadata);
    proto->writeStructBegin(msg, "response");
    proto->writeFieldBegin(msg, "success", FieldType::String, 0);
    proto->writeString(msg, "field");
    proto->writeFieldEnd(msg);
    proto->writeFieldBegin(msg, "", FieldType::Stop, 0);
    proto->writeStructEnd(msg);
    proto->writeMessageEnd(msg);

    TransportPtr transport =
        NamedTransportConfigFactory::getFactory(transport_type).createTransport();
    transport->encodeFrame(buffer, metadata, msg);
  }

  void writeFramedBinaryMessage(Buffer::Instance& buffer, MessageType msg_type, int32_t seq_id) {
    writeMessage(buffer, TransportType::Framed, ProtocolType::Binary, msg_type, seq_id);
  }

  void writeComplexFramedBinaryMessage(Buffer::Instance& buffer, MessageType msg_type,
                                       int32_t seq_id) {
    Buffer::OwnedImpl msg;
    ProtocolPtr proto =
        NamedProtocolConfigFactory::getFactory(ProtocolType::Binary).createProtocol();
    MessageMetadata metadata;
    metadata.setMethodName("name");
    metadata.setMessageType(msg_type);
    metadata.setSequenceId(seq_id);

    proto->writeMessageBegin(msg, metadata);
    proto->writeStructBegin(msg, "wrapper"); // call args struct or response struct
    proto->writeFieldBegin(msg, "wrapper_field", FieldType::Struct, 0); // call arg/response success

    proto->writeStructBegin(msg, "payload");
    proto->writeFieldBegin(msg, "f1", FieldType::Bool, 1);
    proto->writeBool(msg, true);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f2", FieldType::Byte, 2);
    proto->writeByte(msg, 2);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f3", FieldType::Double, 3);
    proto->writeDouble(msg, 3.0);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f4", FieldType::I16, 4);
    proto->writeInt16(msg, 4);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f5", FieldType::I32, 5);
    proto->writeInt32(msg, 5);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f6", FieldType::I64, 6);
    proto->writeInt64(msg, 6);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f7", FieldType::String, 7);
    proto->writeString(msg, "seven");
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f8", FieldType::Map, 8);
    proto->writeMapBegin(msg, FieldType::I32, FieldType::I32, 1);
    proto->writeInt32(msg, 8);
    proto->writeInt32(msg, 8);
    proto->writeMapEnd(msg);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f9", FieldType::List, 9);
    proto->writeListBegin(msg, FieldType::I32, 1);
    proto->writeInt32(msg, 8);
    proto->writeListEnd(msg);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "f10", FieldType::Set, 10);
    proto->writeSetBegin(msg, FieldType::I32, 1);
    proto->writeInt32(msg, 8);
    proto->writeSetEnd(msg);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "", FieldType::Stop, 0); // payload stop field
    proto->writeStructEnd(msg);
    proto->writeFieldEnd(msg);

    proto->writeFieldBegin(msg, "", FieldType::Stop, 0); // wrapper stop field
    proto->writeStructEnd(msg);
    proto->writeMessageEnd(msg);

    TransportPtr transport =
        NamedTransportConfigFactory::getFactory(TransportType::Framed).createTransport();
    transport->encodeFrame(buffer, metadata, msg);
  }

  void writePartialFramedBinaryMessage(Buffer::Instance& buffer, MessageType msg_type,
                                       int32_t seq_id, bool start) {
    Buffer::OwnedImpl frame;
    writeFramedBinaryMessage(frame, msg_type, seq_id);

    if (start) {
      buffer.move(frame, 27);
    } else {
      frame.drain(27);
      buffer.move(frame);
    }
  }

  void writeFramedBinaryTApplicationException(Buffer::Instance& buffer, int32_t seq_id) {
    Buffer::OwnedImpl msg;
    ProtocolPtr proto =
        NamedProtocolConfigFactory::getFactory(ProtocolType::Binary).createProtocol();
    MessageMetadata metadata;
    metadata.setMethodName("name");
    metadata.setMessageType(MessageType::Exception);
    metadata.setSequenceId(seq_id);

    proto->writeMessageBegin(msg, metadata);
    proto->writeStructBegin(msg, "");
    proto->writeFieldBegin(msg, "", FieldType::String, 1);
    proto->writeString(msg, "error");
    proto->writeFieldEnd(msg);
    proto->writeFieldBegin(msg, "", FieldType::I32, 2);
    proto->writeInt32(msg, 1);
    proto->writeFieldEnd(msg);
    proto->writeFieldBegin(msg, "", FieldType::Stop, 0);
    proto->writeStructEnd(msg);
    proto->writeMessageEnd(msg);

    TransportPtr transport =
        NamedTransportConfigFactory::getFactory(TransportType::Framed).createTransport();
    transport->encodeFrame(buffer, metadata, msg);
  }

  void writeFramedBinaryIDLException(Buffer::Instance& buffer, int32_t seq_id) {
    Buffer::OwnedImpl msg;
    ProtocolPtr proto =
        NamedProtocolConfigFactory::getFactory(ProtocolType::Binary).createProtocol();
    MessageMetadata metadata;
    metadata.setMethodName("name");
    metadata.setMessageType(MessageType::Reply);
    metadata.setSequenceId(seq_id);

    proto->writeMessageBegin(msg, metadata);
    proto->writeStructBegin(msg, "");
    proto->writeFieldBegin(msg, "", FieldType::Struct, 2);

    proto->writeStructBegin(msg, "");
    proto->writeFieldBegin(msg, "", FieldType::String, 1);
    proto->writeString(msg, "err");
    proto->writeFieldEnd(msg);
    proto->writeFieldBegin(msg, "", FieldType::Stop, 0);
    proto->writeStructEnd(msg);

    proto->writeFieldEnd(msg);
    proto->writeFieldBegin(msg, "", FieldType::Stop, 0);
    proto->writeStructEnd(msg);
    proto->writeMessageEnd(msg);

    TransportPtr transport =
        NamedTransportConfigFactory::getFactory(TransportType::Framed).createTransport();
    transport->encodeFrame(buffer, metadata, msg);
  }

  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<ThriftFilters::MockDecoderFilter> decoder_filter_;
  Stats::IsolatedStoreImpl store_;
  ThriftFilterStats stats_;
  envoy::config::filter::network::thrift_proxy::v2alpha1::ThriftProxy proto_config_;

  std::unique_ptr<TestConfigImpl> config_;

  Buffer::OwnedImpl buffer_;
  Buffer::OwnedImpl write_buffer_;
  std::unique_ptr<ConnectionManager> filter_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
};

TEST_F(ThriftConnectionManagerTest, OnDataHandlesThriftCall) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(ThriftConnectionManagerTest, OnDataHandlesThriftOneWay) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 0x0F);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_call").value());
  EXPECT_EQ(1U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(ThriftConnectionManagerTest, OnDataHandlesStopIterationAndResume) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, messageBegin(_))
      .WillOnce(Return(ThriftFilters::FilterStatus::StopIteration));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  // Nothing further happens: we're stopped.
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1, callbacks->streamId());
  EXPECT_EQ(TransportType::Framed, callbacks->downstreamTransportType());
  EXPECT_EQ(ProtocolType::Binary, callbacks->downstreamProtocolType());
  EXPECT_EQ(&filter_callbacks_.connection_, callbacks->connection());

  // Resume processing.
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  callbacks->continueDecoding();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_call").value());
  EXPECT_EQ(1U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ThriftConnectionManagerTest, OnDataHandlesFrameSplitAcrossBuffers) {
  initializeFilter();

  writePartialFramedBinaryMessage(buffer_, MessageType::Call, 0x10, true);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0, buffer_.length());

  // Complete the buffer
  writePartialFramedBinaryMessage(buffer_, MessageType::Call, 0x10, false);
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0, buffer_.length());

  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
}

TEST_F(ThriftConnectionManagerTest, OnDataHandlesInvalidMsgType) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Reply, 0x0F); // reply is invalid for a request

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(1U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(ThriftConnectionManagerTest, OnDataHandlesProtocolError) {
  initializeFilter();
  addSeq(buffer_, {
                      0x00, 0x00, 0x00, 0x1f,                     // framed: 31 bytes
                      0x80, 0x01, 0x00, 0x01,                     // binary, call
                      0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                      0x00, 0x00, 0x00, 0x01,                     // sequence id
                      0x08, 0xff, 0xff                            // illegal field id
                  });

  std::string err = "invalid binary protocol field id -1";
  addSeq(write_buffer_, {
                            0x00, 0x00, 0x00, 0x42,                     // framed: 66 bytes
                            0x80, 0x01, 0x00, 0x03,                     // binary, exception
                            0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                            0x00, 0x00, 0x00, 0x01,                     // sequence id
                            0x0b, 0x00, 0x01,                           // begin string field
                        });
  addInt32(write_buffer_, err.length());
  addString(write_buffer_, err);
  addSeq(write_buffer_, {
                            0x08, 0x00, 0x02,       // begin i32 field
                            0x00, 0x00, 0x00, 0x07, // protocol error
                            0x00,                   // stop field
                        });

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(bufferToString(write_buffer_), bufferToString(buffer));
      }));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ThriftConnectionManagerTest, OnDataHandlesProtocolErrorDuringMessageBegin) {
  initializeFilter();
  addSeq(buffer_, {
                      0x00, 0x00, 0x00, 0x1d,                     // framed: 29 bytes
                      0x80, 0x01, 0x00, 0xff,                     // binary, invalid type
                      0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                      0x00, 0x00, 0x00, 0x01,                     // sequence id
                      0x00,                                       // stop field
                  });

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
}

TEST_F(ThriftConnectionManagerTest, OnDataHandlesTransportApplicationException) {
  initializeFilter();
  addSeq(buffer_, {
                      0x00, 0x00, 0x00, 0x64, // header: 100 bytes
                      0x0f, 0xff, 0x00, 0x00, // magic, flags
                      0x00, 0x00, 0x00, 0x01, // sequence id
                      0x00, 0x01, 0x00, 0x02, // header size 4, binary proto, 2 transforms
                      0x01, 0x02, 0x00, 0x00, // transforms: 1, 2; padding
                  });

  std::string err = "Unknown transform 1";
  uint8_t len = 41 + err.length();
  addSeq(write_buffer_, {
                            0x00, 0x00, 0x00, len,  // header frame size
                            0x0f, 0xff, 0x00, 0x00, // magic, flags
                            0x00, 0x00, 0x00, 0x00, // sequence id 0
                            0x00, 0x01, 0x00, 0x00, // header size 4, binary, 0 transforms
                            0x00, 0x00,             // header padding
                            0x80, 0x01, 0x00, 0x03, // binary, exception
                            0x00, 0x00, 0x00, 0x00, // message name ""
                            0x00, 0x00, 0x00, 0x00, // sequence id
                            0x0b, 0x00, 0x01,       // begin string field
                        });
  addInt32(write_buffer_, err.length());
  addString(write_buffer_, err);
  addSeq(write_buffer_, {
                            0x08, 0x00, 0x02,       // begin i32 field
                            0x00, 0x00, 0x00, 0x05, // missing result
                            0x00,                   // stop field
                        });

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(bufferToString(write_buffer_), bufferToString(buffer));
      }));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ThriftConnectionManagerTest, OnEvent) {
  // No active calls
  {
    initializeFilter();
    filter_->onEvent(Network::ConnectionEvent::RemoteClose);
    filter_->onEvent(Network::ConnectionEvent::LocalClose);
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_local_with_active_rq").value());
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_remote_with_active_rq").value());
  }

  // Remote close mid-request
  {
    initializeFilter();
    addSeq(buffer_, {
                        0x00, 0x00, 0x00, 0x1d,                     // framed: 29 bytes
                        0x80, 0x01, 0x00, 0x01,                     // binary proto, call type
                        0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                        0x00, 0x00, 0x00, 0x0F,                     // seq id
                    });
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    filter_->onEvent(Network::ConnectionEvent::RemoteClose);

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  // Local close mid-request
  {
    initializeFilter();
    addSeq(buffer_, {
                        0x00, 0x00, 0x00, 0x1d,                     // framed: 29 bytes
                        0x80, 0x01, 0x00, 0x01,                     // binary proto, call type
                        0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                        0x00, 0x00, 0x00, 0x0F,                     // seq id
                    });
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    filter_->onEvent(Network::ConnectionEvent::LocalClose);

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    buffer_.drain(buffer_.length());

    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  // Remote close before response
  {
    initializeFilter();
    writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    filter_->onEvent(Network::ConnectionEvent::RemoteClose);

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_remote_with_active_rq").value());

    buffer_.drain(buffer_.length());

    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  // Local close before response
  {
    initializeFilter();
    writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
    filter_->onEvent(Network::ConnectionEvent::LocalClose);

    EXPECT_EQ(1U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    buffer_.drain(buffer_.length());

    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }
}

TEST_F(ThriftConnectionManagerTest, Routing) {
  const std::string yaml = R"EOF(
transport: FRAMED
protocol: BINARY
stat_prefix: test
route_config:
  name: "routes"
  routes:
    - match:
        method_name: name
      route:
        cluster: cluster
)EOF";

  initializeFilter(yaml);
  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, messageBegin(_))
      .WillOnce(Return(ThriftFilters::FilterStatus::StopIteration));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  Router::RouteConstSharedPtr route = callbacks->route();
  EXPECT_NE(nullptr, route);
  EXPECT_NE(nullptr, route->routeEntry());
  EXPECT_EQ("cluster", route->routeEntry()->clusterName());

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  callbacks->continueDecoding();

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
}

TEST_F(ThriftConnectionManagerTest, RequestAndResponse) {
  initializeFilter();
  writeComplexFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  writeComplexFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x0F);

  callbacks->startUpstreamResponse(TransportType::Framed, ProtocolType::Binary);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(true, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
}

TEST_F(ThriftConnectionManagerTest, RequestAndExceptionResponse) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  writeFramedBinaryTApplicationException(write_buffer_, 0x0F);

  callbacks->startUpstreamResponse(TransportType::Framed, ProtocolType::Binary);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(true, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
}

TEST_F(ThriftConnectionManagerTest, RequestAndErrorResponse) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  writeFramedBinaryIDLException(write_buffer_, 0x0F);

  callbacks->startUpstreamResponse(TransportType::Framed, ProtocolType::Binary);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(true, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(1U, store_.counter("test.response_error").value());
}

TEST_F(ThriftConnectionManagerTest, RequestAndInvalidResponse) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  // Call is not valid in a response
  writeFramedBinaryMessage(write_buffer_, MessageType::Call, 0x0F);

  callbacks->startUpstreamResponse(TransportType::Framed, ProtocolType::Binary);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(true, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(1U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
}

TEST_F(ThriftConnectionManagerTest, RequestAndResponseProtocolError) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  // illegal field id
  addSeq(write_buffer_, {
                            0x00, 0x00, 0x00, 0x1f,                     // framed: 31 bytes
                            0x80, 0x01, 0x00, 0x02,                     // binary, reply
                            0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                            0x00, 0x00, 0x00, 0x01,                     // sequence id
                            0x08, 0xff, 0xff                            // illegal field id
                        });

  callbacks->startUpstreamResponse(TransportType::Framed, ProtocolType::Binary);

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_CALL(*decoder_filter_, resetUpstreamConnection());
  EXPECT_EQ(true, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
}

TEST_F(ThriftConnectionManagerTest, RequestAndTransportApplicationException) {
  initializeFilter();
  writeMessage(buffer_, TransportType::Header, ProtocolType::Binary, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  // Response with unknown transform
  addSeq(write_buffer_, {
                            0x00, 0x00, 0x00, 0x64, // header: 100 bytes
                            0x0f, 0xff, 0x00, 0x00, // magic, flags
                            0x00, 0x00, 0x00, 0x01, // sequence id
                            0x00, 0x01, 0x00, 0x02, // header size 4, binary proto, 2 transforms
                            0x01, 0x02, 0x00, 0x00, // transforms: 1, 2; padding
                        });

  callbacks->startUpstreamResponse(TransportType::Header, ProtocolType::Binary);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(1);
  EXPECT_EQ(true, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
}

TEST_F(ThriftConnectionManagerTest, PipelinedRequestAndResponse) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x01);
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x02);

  std::list<ThriftFilters::DecoderFilterCallbacks*> callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillRepeatedly(Invoke(
          [&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks.push_back(&cb); }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(2U, store_.gauge("test.request_active").value());
  EXPECT_EQ(2U, store_.counter("test.request").value());
  EXPECT_EQ(2U, store_.counter("test.request_call").value());

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(2);

  writeFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x01);
  callbacks.front()->startUpstreamResponse(TransportType::Framed, ProtocolType::Binary);
  EXPECT_EQ(true, callbacks.front()->upstreamData(write_buffer_));
  callbacks.pop_front();
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());

  writeFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x02);
  callbacks.front()->startUpstreamResponse(TransportType::Framed, ProtocolType::Binary);
  EXPECT_EQ(true, callbacks.front()->upstreamData(write_buffer_));
  callbacks.pop_front();
  EXPECT_EQ(2U, store_.counter("test.response").value());
  EXPECT_EQ(2U, store_.counter("test.response_reply").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

TEST_F(ThriftConnectionManagerTest, ResetDownstreamConnection) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(1U, store_.gauge("test.request_active").value());

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  callbacks->resetDownstreamConnection();

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, store_.gauge("test.request_active").value());
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
