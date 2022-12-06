#include <memory>

#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/v3/thrift_proxy.pb.validate.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/network/thrift_proxy/binary_protocol_impl.h"
#include "source/extensions/filters/network/thrift_proxy/buffer_helper.h"
#include "source/extensions/filters/network/thrift_proxy/config.h"
#include "source/extensions/filters/network/thrift_proxy/conn_manager.h"
#include "source/extensions/filters/network/thrift_proxy/framed_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/header_transport_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/rds_impl.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/test_common/printers.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Ref;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class TestConfigImpl : public ConfigImpl {
public:
  TestConfigImpl(envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy proto_config,
                 Server::Configuration::MockFactoryContext& context,
                 Router::RouteConfigProviderManager& route_config_provider_manager,
                 ThriftFilters::DecoderFilterSharedPtr decoder_filter,
                 ThriftFilters::EncoderFilterSharedPtr encoder_filter,
                 ThriftFilters::BidirectionalFilterSharedPtr bidirectional_filter,
                 ThriftFilterStats& stats)
      : ConfigImpl(proto_config, context, route_config_provider_manager),
        decoder_filter_(decoder_filter), encoder_filter_(encoder_filter),
        bidirectional_filter_(bidirectional_filter), stats_(stats) {}

  // ConfigImpl
  ThriftFilterStats& stats() override { return stats_; }
  void createFilterChain(ThriftFilters::FilterChainFactoryCallbacks& callbacks) override {
    if (custom_decoder_filter_) {
      callbacks.addDecoderFilter(custom_decoder_filter_);
    }
    callbacks.addDecoderFilter(decoder_filter_);
    if (custom_encoder_filter_) {
      callbacks.addEncoderFilter(custom_encoder_filter_);
    }
    callbacks.addEncoderFilter(encoder_filter_);
    callbacks.addBidirectionalFilter(bidirectional_filter_);
  }
  TransportPtr createTransport() override {
    if (transport_) {
      return TransportPtr{transport_};
    }
    return ConfigImpl::createTransport();
  }
  ProtocolPtr createProtocol() override {
    if (protocol_) {
      return ProtocolPtr{protocol_};
    }
    return ConfigImpl::createProtocol();
  }

  ThriftFilters::DecoderFilterSharedPtr custom_decoder_filter_;
  ThriftFilters::DecoderFilterSharedPtr decoder_filter_;
  ThriftFilters::EncoderFilterSharedPtr custom_encoder_filter_;
  ThriftFilters::EncoderFilterSharedPtr encoder_filter_;
  ThriftFilters::BidirectionalFilterSharedPtr bidirectional_filter_;
  ThriftFilterStats& stats_;
  MockTransport* transport_{};
  MockProtocol* protocol_{};
};

class ThriftConnectionManagerTest : public testing::Test {
public:
  ThriftConnectionManagerTest() : stats_(ThriftFilterStats::generateStats("test.", store_)) {
    route_config_provider_manager_ =
        std::make_unique<Router::RouteConfigProviderManagerImpl>(context_.admin_);
    ON_CALL(*context_.access_log_manager_.file_, write(_))
        .WillByDefault(SaveArg<0>(&access_log_data_));
  }
  ~ThriftConnectionManagerTest() override {
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  }

  void initializeFilterWithCustomFilters() {
    auto* decoder_filter = new NiceMock<ThriftFilters::MockDecoderFilter>();
    custom_decoder_filter_.reset(decoder_filter);
    auto* encoder_filter = new NiceMock<ThriftFilters::MockEncoderFilter>();
    custom_encoder_filter_.reset(encoder_filter);
    initializeFilter();
  }
  void initializeFilter() { initializeFilter(""); }

  std::string accessLogConfig(bool drain_response_header = false) const {
    std::vector<std::string> fields = {
        "%DYNAMIC_METADATA(thrift.proxy:method)%",
        "%DYNAMIC_METADATA(thrift.proxy:cluster)%",
        "passthrough_enabled=%DYNAMIC_METADATA(thrift.proxy:passthrough)%",
        "%DYNAMIC_METADATA(thrift.proxy:request:transport_type)%",
        "%DYNAMIC_METADATA(thrift.proxy:request:protocol_type)%",
        "%DYNAMIC_METADATA(thrift.proxy:request:message_type)%",
        "%DYNAMIC_METADATA(thrift.proxy:response:transport_type)%",
        "%DYNAMIC_METADATA(thrift.proxy:response:protocol_type)%",
        "%DYNAMIC_METADATA(thrift.proxy:response:message_type)%",
        "%DYNAMIC_METADATA(thrift.proxy:response:reply_type)%",
        "%BYTES_RECEIVED%",
        "%BYTES_SENT%",
        "%DURATION%",
        "%UPSTREAM_HOST%",
    };

    if (drain_response_header) {
      fields.push_back("%RESP(:drain)%");
    }

    return fmt::format(R"EOF(
access_log:
  - name: accesslog
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.access_loggers.file.v3.FileAccessLog
      path: /dev/null
      log_format:
        text_format_source:
          inline_string: "{}\n"
)EOF",
                       absl::StrJoin(fields, " "));
  }

  std::string defaultYamlConfig(bool drain_response_header = false) {
    return fmt::format(R"EOF(
route_config:
  name: routes
  validate_clusters: false
  routes:
    - match:
        method_name: name
      route:
        cluster: cluster
stat_prefix: test
{}
)EOF",
                       accessLogConfig(drain_response_header));
  }

  void initializeFilter(const std::string& yaml,
                        const std::vector<std::string>& cluster_names = {}) {
    envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config;
    if (yaml.empty()) {
      TestUtility::loadFromYaml(defaultYamlConfig(), config);
    } else {
      TestUtility::loadFromYaml(yaml, config);
    }

    TestUtility::validate(config);

    initializeFilter(config, cluster_names);
  }
  void initializeFilter(envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy& config,
                        const std::vector<std::string>& cluster_names = {}) {
    // Destroy any existing filter first.
    filter_ = nullptr;

    for (const auto& counter : store_.counters()) {
      counter->reset();
    }

    decoder_filter_ = std::make_shared<NiceMock<ThriftFilters::MockDecoderFilter>>();

    context_.server_factory_context_.cluster_manager_.initializeClusters(cluster_names, {});

    proto_config_ = config;
    decoder_filter_ = std::make_shared<NiceMock<ThriftFilters::MockDecoderFilter>>();
    encoder_filter_ = std::make_shared<NiceMock<ThriftFilters::MockEncoderFilter>>();
    bidirectional_filter_ = std::make_shared<NiceMock<ThriftFilters::MockBidirectionalFilter>>();

    config_ = std::make_unique<TestConfigImpl>(proto_config_, context_,
                                               *route_config_provider_manager_, decoder_filter_,
                                               encoder_filter_, bidirectional_filter_, stats_);
    if (custom_transport_) {
      config_->transport_ = custom_transport_;
    }
    if (custom_protocol_) {
      config_->protocol_ = custom_protocol_;
    }
    if (custom_decoder_filter_) {
      config_->custom_decoder_filter_ = custom_decoder_filter_;
    }
    if (custom_encoder_filter_) {
      config_->custom_encoder_filter_ = custom_encoder_filter_;
    }

    ON_CALL(random_, random()).WillByDefault(Return(42));
    filter_ = std::make_unique<ConnectionManager>(
        *config_, random_, filter_callbacks_.connection_.dispatcher_.timeSource(), drain_decision_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    ON_CALL(filter_callbacks_.connection_.stream_info_, setDynamicMetadata(_, _))
        .WillByDefault(Invoke([this](const std::string& key, const ProtobufWkt::Struct& obj) {
          (*filter_callbacks_.connection_.stream_info_.metadata_.mutable_filter_metadata())[key]
              .MergeFrom(obj);
        }));
    filter_->onNewConnection();

    // NOP currently.
    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  }

  // Return the number of requests actually sent.
  uint32_t sendRequests(uint32_t request_number) {
    for (uint32_t i = 0; i < request_number; i++) {
      writeComplexFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);
      writeComplexFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x0F);

      ThriftFilters::DecoderFilterCallbacks* callbacks{};
      ON_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
          .WillByDefault(
              Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

      EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

      if (!callbacks) {
        return i;
      }

      FramedTransportImpl transport;
      BinaryProtocolImpl proto;
      callbacks->startUpstreamResponse(transport, proto);

      EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
      EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));
    }
    return request_number;
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
    MessageMetadata metadata(msg_type == MessageType::Call);
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

  void writeVoidFramedBinaryMessage(Buffer::Instance& buffer, int32_t seq_id) {
    Buffer::OwnedImpl msg;
    ProtocolPtr proto =
        NamedProtocolConfigFactory::getFactory(ProtocolType::Binary).createProtocol();
    MessageMetadata metadata;
    metadata.setMethodName("name");
    metadata.setMessageType(MessageType::Reply);
    metadata.setSequenceId(seq_id);

    proto->writeMessageBegin(msg, metadata);
    proto->writeStructBegin(msg, "");
    proto->writeFieldBegin(msg, "", FieldType::Stop, 0);
    proto->writeStructEnd(msg);
    proto->writeMessageEnd(msg);

    TransportPtr transport =
        NamedTransportConfigFactory::getFactory(TransportType::Framed).createTransport();
    transport->encodeFrame(buffer, metadata, msg);
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
    // successful response struct in field id 0, error (IDL exception) in field id greater than 0
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

  void testRequestResponse(bool draining = false) {
    TestScopedRuntime scoped_runtime;

    if (draining) {
      scoped_runtime.mergeValues(
          {{"envoy.reloadable_features.thrift_connection_draining", "true"}});
      EXPECT_CALL(drain_decision_, drainClose()).WillOnce(Return(true));
    }

    initializeFilter(defaultYamlConfig(true));
    writeComplexFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

    checkDecoderEventsCalledToFilters(MessageType::Call, 0x0F, MessageType::Reply, 0x0F);

    ThriftFilters::DecoderFilterCallbacks* callbacks{};
    EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
        .WillOnce(
            Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
    EXPECT_EQ(1U, store_.counter("test.request_call").value());

    writeComplexFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x0F);

    FramedTransportImpl transport;
    BinaryProtocolImpl proto;
    callbacks->startUpstreamResponse(transport, proto);

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
    EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

    const auto header =
        callbacks->responseMetadata()->responseHeaders().get(ThriftProxy::Headers::get().Drain);

    if (draining) {
      EXPECT_FALSE(header.empty());
      EXPECT_EQ("true", header[0]->value().getStringView());
    } else {
      EXPECT_TRUE(header.empty());
    }

    // This should not be incremented during a close triggered by a drain.
    EXPECT_EQ(0U, store_.counter("test.cx_destroy_local_with_active_rq").value());

    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    EXPECT_EQ(1U, store_.counter("test.request").value());
    EXPECT_EQ(1U, store_.counter("test.request_call").value());
    EXPECT_EQ(0U, stats_.request_active_.value());
    EXPECT_EQ(1U, store_.counter("test.response").value());
    EXPECT_EQ(1U, store_.counter("test.response_reply").value());
    EXPECT_EQ(0U, store_.counter("test.response_exception").value());
    EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
    EXPECT_EQ(1U, store_.counter("test.response_success").value());
    EXPECT_EQ(0U, store_.counter("test.response_error").value());
    EXPECT_EQ(draining ? 1U : 0U, store_.counter("test.downstream_response_drain_close").value());

    std::string expected_access_log =
        fmt::format("name cluster passthrough_enabled=false framed binary call framed binary reply "
                    "success 0 0 0 - {}\n",
                    draining ? "true" : "-");
    EXPECT_EQ(access_log_data_.value(), expected_access_log);
  }

  void checkDecoderEventsCalledToFilters(MessageType req_msg_type, int32_t req_seq_id,
                                         MessageType resp_msg_type, int32_t resp_seq_id) {
    bool one = true;
    uint8_t two = 2;
    double three = 3.0;
    int16_t four = 4;
    int32_t five = 5;
    int64_t six = 6;
    int32_t eight = 8;
    FieldType field_type_i32 = FieldType::I32;

    uint32_t one_32 = 1;
    EXPECT_CALL(*decoder_filter_, messageBegin(_))
        .WillOnce(
            Invoke([req_msg_type, req_seq_id](MessageMetadataSharedPtr metadata) -> FilterStatus {
              EXPECT_TRUE(metadata->hasMethodName());
              EXPECT_TRUE(metadata->hasMessageType());
              EXPECT_TRUE(metadata->hasSequenceId());
              EXPECT_EQ("name", metadata->methodName());
              EXPECT_EQ(req_msg_type, metadata->messageType());
              EXPECT_EQ(req_seq_id, metadata->sequenceId());
              return FilterStatus::Continue;
            }));
    EXPECT_CALL(*decoder_filter_, messageEnd());
    // The struct name is not available at runtime.
    EXPECT_CALL(*decoder_filter_, structBegin("")).Times(2);
    EXPECT_CALL(*decoder_filter_, structEnd()).Times(2);
    // The field name is not available at runtime.
    EXPECT_CALL(*decoder_filter_, fieldBegin("", _, _)).Times(11);
    EXPECT_CALL(*decoder_filter_, fieldEnd()).Times(11);
    EXPECT_CALL(*decoder_filter_, boolValue(one));
    EXPECT_CALL(*decoder_filter_, byteValue(two));
    EXPECT_CALL(*decoder_filter_, doubleValue(three));
    EXPECT_CALL(*decoder_filter_, int16Value(four));
    EXPECT_CALL(*decoder_filter_, int32Value(five));
    EXPECT_CALL(*decoder_filter_, int64Value(six));
    EXPECT_CALL(*decoder_filter_, stringValue("seven"));
    EXPECT_CALL(*decoder_filter_, mapBegin(field_type_i32, field_type_i32, one_32));
    EXPECT_CALL(*decoder_filter_, int32Value(eight)).Times(4);
    EXPECT_CALL(*decoder_filter_, mapEnd());
    EXPECT_CALL(*decoder_filter_, listBegin(field_type_i32, one_32));
    EXPECT_CALL(*decoder_filter_, listEnd());
    EXPECT_CALL(*decoder_filter_, setBegin(field_type_i32, one_32));
    EXPECT_CALL(*decoder_filter_, setEnd());

    EXPECT_CALL(*encoder_filter_, transportBegin(_));
    EXPECT_CALL(*encoder_filter_, transportEnd());
    EXPECT_CALL(*encoder_filter_, messageBegin(_))
        .WillOnce(
            Invoke([resp_msg_type, resp_seq_id](MessageMetadataSharedPtr metadata) -> FilterStatus {
              EXPECT_TRUE(metadata->hasMethodName());
              EXPECT_TRUE(metadata->hasMessageType());
              EXPECT_TRUE(metadata->hasSequenceId());
              EXPECT_EQ("name", metadata->methodName());
              EXPECT_EQ(resp_msg_type, metadata->messageType());
              EXPECT_EQ(resp_seq_id, metadata->sequenceId());
              return FilterStatus::Continue;
            }));
    EXPECT_CALL(*encoder_filter_, messageEnd());
    EXPECT_CALL(*encoder_filter_, structBegin("")).Times(2);
    EXPECT_CALL(*encoder_filter_, structEnd()).Times(2);
    EXPECT_CALL(*encoder_filter_, fieldBegin("", _, _)).Times(11);
    EXPECT_CALL(*encoder_filter_, fieldEnd()).Times(11);
    EXPECT_CALL(*encoder_filter_, boolValue(one));
    EXPECT_CALL(*encoder_filter_, byteValue(two));
    EXPECT_CALL(*encoder_filter_, doubleValue(three));
    EXPECT_CALL(*encoder_filter_, int16Value(four));
    EXPECT_CALL(*encoder_filter_, int32Value(five));
    EXPECT_CALL(*encoder_filter_, int64Value(six));
    EXPECT_CALL(*encoder_filter_, stringValue("seven"));
    EXPECT_CALL(*encoder_filter_, mapBegin(field_type_i32, field_type_i32, one_32));
    EXPECT_CALL(*encoder_filter_, int32Value(eight)).Times(4);
    EXPECT_CALL(*encoder_filter_, mapEnd());
    EXPECT_CALL(*encoder_filter_, listBegin(field_type_i32, one_32));
    EXPECT_CALL(*encoder_filter_, listEnd());
    EXPECT_CALL(*encoder_filter_, setBegin(field_type_i32, one_32));
    EXPECT_CALL(*encoder_filter_, setEnd());

    EXPECT_CALL(*bidirectional_filter_, decodeMessageBegin(_))
        .WillOnce(
            Invoke([req_msg_type, req_seq_id](MessageMetadataSharedPtr metadata) -> FilterStatus {
              EXPECT_TRUE(metadata->hasMethodName());
              EXPECT_TRUE(metadata->hasMessageType());
              EXPECT_TRUE(metadata->hasSequenceId());
              EXPECT_EQ("name", metadata->methodName());
              EXPECT_EQ(req_msg_type, metadata->messageType());
              EXPECT_EQ(req_seq_id, metadata->sequenceId());
              return FilterStatus::Continue;
            }));
    EXPECT_CALL(*bidirectional_filter_, decodeMessageEnd());
    EXPECT_CALL(*bidirectional_filter_, decodeStructBegin("")).Times(2);
    EXPECT_CALL(*bidirectional_filter_, decodeStructEnd()).Times(2);
    EXPECT_CALL(*bidirectional_filter_, decodeFieldBegin("", _, _)).Times(11);
    EXPECT_CALL(*bidirectional_filter_, decodeFieldEnd()).Times(11);
    EXPECT_CALL(*bidirectional_filter_, decodeBoolValue(one));
    EXPECT_CALL(*bidirectional_filter_, decodeByteValue(two));
    EXPECT_CALL(*bidirectional_filter_, decodeDoubleValue(three));
    EXPECT_CALL(*bidirectional_filter_, decodeInt16Value(four));
    EXPECT_CALL(*bidirectional_filter_, decodeInt32Value(five));
    EXPECT_CALL(*bidirectional_filter_, decodeInt64Value(six));
    EXPECT_CALL(*bidirectional_filter_, decodeStringValue("seven"));
    EXPECT_CALL(*bidirectional_filter_, decodeMapBegin(field_type_i32, field_type_i32, one_32));
    EXPECT_CALL(*bidirectional_filter_, decodeInt32Value(eight)).Times(4);
    EXPECT_CALL(*bidirectional_filter_, decodeMapEnd());
    EXPECT_CALL(*bidirectional_filter_, decodeListBegin(field_type_i32, one_32));
    EXPECT_CALL(*bidirectional_filter_, decodeListEnd());
    EXPECT_CALL(*bidirectional_filter_, decodeSetBegin(field_type_i32, one_32));
    EXPECT_CALL(*bidirectional_filter_, decodeSetEnd());

    EXPECT_CALL(*bidirectional_filter_, encodeTransportBegin(_));
    EXPECT_CALL(*bidirectional_filter_, encodeTransportEnd());
    EXPECT_CALL(*bidirectional_filter_, encodeMessageBegin(_))
        .WillOnce(
            Invoke([resp_msg_type, resp_seq_id](MessageMetadataSharedPtr metadata) -> FilterStatus {
              EXPECT_TRUE(metadata->hasMethodName());
              EXPECT_TRUE(metadata->hasMessageType());
              EXPECT_TRUE(metadata->hasSequenceId());
              EXPECT_EQ("name", metadata->methodName());
              EXPECT_EQ(resp_msg_type, metadata->messageType());
              EXPECT_EQ(resp_seq_id, metadata->sequenceId());
              return FilterStatus::Continue;
            }));
    EXPECT_CALL(*bidirectional_filter_, encodeMessageEnd());
    EXPECT_CALL(*bidirectional_filter_, encodeStructBegin("")).Times(2);
    EXPECT_CALL(*bidirectional_filter_, encodeStructEnd()).Times(2);
    EXPECT_CALL(*bidirectional_filter_, encodeFieldBegin("", _, _)).Times(11);
    EXPECT_CALL(*bidirectional_filter_, encodeFieldEnd()).Times(11);
    EXPECT_CALL(*bidirectional_filter_, encodeBoolValue(one));
    EXPECT_CALL(*bidirectional_filter_, encodeByteValue(two));
    EXPECT_CALL(*bidirectional_filter_, encodeDoubleValue(three));
    EXPECT_CALL(*bidirectional_filter_, encodeInt16Value(four));
    EXPECT_CALL(*bidirectional_filter_, encodeInt32Value(five));
    EXPECT_CALL(*bidirectional_filter_, encodeInt64Value(six));
    EXPECT_CALL(*bidirectional_filter_, encodeStringValue("seven"));
    EXPECT_CALL(*bidirectional_filter_, encodeMapBegin(field_type_i32, field_type_i32, one_32));
    EXPECT_CALL(*bidirectional_filter_, encodeInt32Value(eight)).Times(4);
    EXPECT_CALL(*bidirectional_filter_, encodeMapEnd());
    EXPECT_CALL(*bidirectional_filter_, encodeListBegin(field_type_i32, one_32));
    EXPECT_CALL(*bidirectional_filter_, encodeListEnd());
    EXPECT_CALL(*bidirectional_filter_, encodeSetBegin(field_type_i32, one_32));
    EXPECT_CALL(*bidirectional_filter_, encodeSetEnd());
  }

  void passthroughSupportedSetup(bool expected_decode_passthrough_data_called = true,
                                 bool expected_encode_passthrough_data_called = true) {
    EXPECT_CALL(*decoder_filter_, passthroughSupported()).WillRepeatedly(Return(true));
    EXPECT_CALL(*encoder_filter_, passthroughSupported()).WillRepeatedly(Return(true));
    EXPECT_CALL(*bidirectional_filter_, decodePassthroughSupported()).WillRepeatedly(Return(true));
    EXPECT_CALL(*bidirectional_filter_, encodePassthroughSupported()).WillRepeatedly(Return(true));

    if (expected_decode_passthrough_data_called) {
      EXPECT_CALL(*decoder_filter_, passthroughData(_));
      EXPECT_CALL(*bidirectional_filter_, decodePassthroughData(_));
    } else {
      EXPECT_CALL(*decoder_filter_, passthroughData(_)).Times(0);
      EXPECT_CALL(*bidirectional_filter_, decodePassthroughData(_)).Times(0);
    }

    if (expected_encode_passthrough_data_called) {
      EXPECT_CALL(*encoder_filter_, passthroughData(_));
      EXPECT_CALL(*bidirectional_filter_, encodePassthroughData(_));
    } else {
      EXPECT_CALL(*encoder_filter_, passthroughData(_)).Times(0);
      EXPECT_CALL(*bidirectional_filter_, encodePassthroughData(_)).Times(0);
    }
  }

  StringViewSaver access_log_data_;
  NiceMock<Server::Configuration::MockFactoryContext> context_;
  std::shared_ptr<ThriftFilters::MockDecoderFilter> decoder_filter_;
  std::shared_ptr<ThriftFilters::MockEncoderFilter> encoder_filter_;
  std::shared_ptr<ThriftFilters::MockBidirectionalFilter> bidirectional_filter_;
  Stats::TestUtil::TestStore store_;
  ThriftFilterStats stats_;
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy proto_config_;

  std::unique_ptr<Router::RouteConfigProviderManagerImpl> route_config_provider_manager_;
  std::unique_ptr<TestConfigImpl> config_;

  Buffer::OwnedImpl buffer_;
  Buffer::OwnedImpl write_buffer_;
  NiceMock<Network::MockReadFilterCallbacks> filter_callbacks_;
  NiceMock<Random::MockRandomGenerator> random_;
  NiceMock<Network::MockDrainDecision> drain_decision_;
  std::unique_ptr<ConnectionManager> filter_;
  MockTransport* custom_transport_{};
  MockProtocol* custom_protocol_{};
  std::shared_ptr<ThriftFilters::MockDecoderFilter> custom_decoder_filter_;
  std::shared_ptr<ThriftFilters::MockEncoderFilter> custom_encoder_filter_;
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
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());

  EXPECT_EQ(access_log_data_, "");
}

TEST_F(ThriftConnectionManagerTest, OnDataHandlesThriftOneWay) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 0x0F);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_call").value());
  EXPECT_EQ(1U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());

  EXPECT_EQ(access_log_data_,
            "name cluster passthrough_enabled=false framed binary oneway - - - - 0 0 0 -\n");
}

TEST_F(ThriftConnectionManagerTest, OnDataHandlesStopIterationAndResume) {
  initializeFilter();

  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, messageBegin(_)).WillOnce(Return(FilterStatus::StopIteration));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());

  // Nothing further happens: we're stopped.
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(42, callbacks->streamId());
  EXPECT_EQ(TransportType::Framed, callbacks->downstreamTransportType());
  EXPECT_EQ(ProtocolType::Binary, callbacks->downstreamProtocolType());
  EXPECT_EQ(&filter_callbacks_.connection_, callbacks->connection());

  // Resume processing.
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  callbacks->continueDecoding();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_call").value());
  EXPECT_EQ(1U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, stats_.request_active_.value());

  EXPECT_EQ(access_log_data_,
            "name cluster passthrough_enabled=false framed binary oneway - - - - 0 0 0 -\n");
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

  EXPECT_EQ(access_log_data_, "");
}

TEST_F(ThriftConnectionManagerTest, OnDataHandlesInvalidMsgType) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Reply, 0x0F); // reply is invalid for a request

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(1U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());

  EXPECT_EQ(access_log_data_, "");
}

TEST_F(ThriftConnectionManagerTest, OnDataHandlesProtocolError) {
  initializeFilter();
  addSeq(buffer_, {
                      0x00, 0x00, 0x00, 0x1f,                     // framed: 31 bytes
                      0x80, 0x01, 0x00, 0x01,                     // binary, call
                      0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                      0x00, 0x00, 0x00, 0x01,                     // sequence id
                      0x0d, 0x00, 0x01, 0x0b, 0xb,                // map, field id, string key/value
                      0xff, 0xff, 0xff, 0xff,                     // negative length
                  });

  std::string err = "negative binary protocol map size -1";
  addSeq(write_buffer_, {
                            0x00, 0x00, 0x00, 0x43,                     // framed: 67 bytes
                            0x80, 0x01, 0x00, 0x03,                     // binary, exception
                            0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                            0x00, 0x00, 0x00, 0x01,                     // sequence id
                            0x0b, 0x00, 0x01,                           // begin string field
                        });
  write_buffer_.writeBEInt<uint32_t>(err.length());
  write_buffer_.add(err);
  addSeq(write_buffer_, {
                            0x08, 0x00, 0x02,       // begin i32 field
                            0x00, 0x00, 0x00, 0x07, // protocol error
                            0x00,                   // stop field
                        });

  EXPECT_CALL(filter_callbacks_.connection_, write(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(write_buffer_.toString(), buffer.toString());
      }));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, stats_.request_active_.value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, stats_.request_active_.value());

  EXPECT_EQ(access_log_data_,
            "name cluster passthrough_enabled=false framed binary call - - - - 0 0 0 -\n");
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

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());

  EXPECT_EQ(access_log_data_, "");
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
  write_buffer_.writeBEInt<int32_t>(err.length());
  write_buffer_.add(err);
  addSeq(write_buffer_, {
                            0x08, 0x00, 0x02,       // begin i32 field
                            0x00, 0x00, 0x00, 0x05, // missing result
                            0x00,                   // stop field
                        });

  EXPECT_CALL(filter_callbacks_.connection_, write(_, true))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(write_buffer_.toString(), buffer.toString());
      }));
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0U, stats_.request_active_.value());

  EXPECT_EQ(access_log_data_, "");
}

// Tests that OnData handles non-thrift input. Regression test for crash on invalid input.
TEST_F(ThriftConnectionManagerTest, OnDataHandlesGarbageRequest) {
  initializeFilter();
  addRepeated(buffer_, 8, 0);
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0U, stats_.request_active_.value());

  EXPECT_EQ(access_log_data_, "");
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

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
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

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
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

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
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

    EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
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

  initializeFilter(yaml, {"cluster"});
  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, messageBegin(_)).WillOnce(Return(FilterStatus::StopIteration));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());

  Router::RouteConstSharedPtr route = callbacks->route();
  EXPECT_NE(nullptr, route);
  EXPECT_NE(nullptr, route->routeEntry());
  EXPECT_EQ("cluster", route->routeEntry()->clusterName());

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  callbacks->continueDecoding();

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(access_log_data_, "");
}

TEST_F(ThriftConnectionManagerTest, RequestAndResponse) { testRequestResponse(false); }

TEST_F(ThriftConnectionManagerTest, RequestAndResponseDraining) { testRequestResponse(true); }

TEST_F(ThriftConnectionManagerTest, RequestAndVoidResponse) {
  initializeFilter();
  writeComplexFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  writeVoidFramedBinaryMessage(write_buffer_, 0x0F);

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());

  EXPECT_EQ(access_log_data_, "name cluster passthrough_enabled=false framed binary call framed "
                              "binary reply success 0 0 0 -\n");
}

// Tests that the downstream request's sequence number is used for the response.
TEST_F(ThriftConnectionManagerTest, RequestAndResponseSequenceIdHandling) {
  initializeFilter();
  writeComplexFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  writeComplexFramedBinaryMessage(write_buffer_, MessageType::Reply, 0xFF);

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  Buffer::OwnedImpl response_buffer;
  writeComplexFramedBinaryMessage(response_buffer, MessageType::Reply, 0x0F);

  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(response_buffer.toString(), buffer.toString());
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());

  EXPECT_EQ(access_log_data_, "name cluster passthrough_enabled=false framed binary call framed "
                              "binary reply success 0 0 0 -\n");
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

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());

  EXPECT_EQ(access_log_data_, "name cluster passthrough_enabled=false framed binary call framed "
                              "binary exception - 0 0 0 -\n");
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

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(1U, store_.counter("test.response_error").value());

  EXPECT_EQ(access_log_data_, "name cluster passthrough_enabled=false framed binary call framed "
                              "binary reply error 0 0 0 -\n");
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

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(1U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());

  EXPECT_EQ(
      access_log_data_,
      "name cluster passthrough_enabled=false framed binary call framed binary call - 0 0 0 -\n");
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

  // illegal negative set length
  addSeq(write_buffer_, {
                            0x00, 0x00, 0x00, 0x2f,                     // framed: 31 bytes
                            0x80, 0x01, 0x00, 0x02,                     // binary, reply
                            0x00, 0x00, 0x00, 0x04, 'n', 'a', 'm', 'e', // message name
                            0x00, 0x00, 0x00, 0x01,                     // sequence id
                            0x0e, 0x00, 0x00, 0x0b,                     // set, field id, strings
                            0xff, 0xff, 0xff, 0xff,                     // negative length
                        });

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  EXPECT_CALL(filter_callbacks_.connection_, write(_, true));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Reset, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(1U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());

  EXPECT_EQ(access_log_data_, "name cluster passthrough_enabled=false framed binary call framed "
                              "binary reply success 0 0 0 -\n");
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

  HeaderTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Reset, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(1U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());

  EXPECT_EQ(access_log_data_,
            "name cluster passthrough_enabled=false header binary call - - - - 0 0 0 -\n");
}

// Tests that a request is routed and a non-thrift response is handled.
TEST_F(ThriftConnectionManagerTest, RequestAndGarbageResponse) {
  initializeFilter();
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  addRepeated(write_buffer_, 8, 0);

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Reset, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(1U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());

  EXPECT_EQ(access_log_data_,
            "name cluster passthrough_enabled=false framed binary call - - - - 0 0 0 -\n");
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
  EXPECT_EQ(2U, stats_.request_active_.value());
  EXPECT_EQ(2U, store_.counter("test.request").value());
  EXPECT_EQ(2U, store_.counter("test.request_call").value());

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_)).Times(2);

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;

  writeFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x01);
  callbacks.front()->startUpstreamResponse(transport, proto);
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete,
            callbacks.front()->upstreamData(write_buffer_));
  callbacks.pop_front();
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());

  writeFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x02);
  callbacks.front()->startUpstreamResponse(transport, proto);
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete,
            callbacks.front()->upstreamData(write_buffer_));
  callbacks.pop_front();
  EXPECT_EQ(2U, store_.counter("test.response").value());
  EXPECT_EQ(2U, store_.counter("test.response_reply").value());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, stats_.request_active_.value());

  EXPECT_EQ(access_log_data_, "name cluster passthrough_enabled=false framed binary call framed "
                              "binary reply success 0 0 0 -\n");
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
  EXPECT_EQ(1U, stats_.request_active_.value());

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::NoFlush));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  callbacks->resetDownstreamConnection();

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(0U, stats_.request_active_.value());
}

// Test the base case where there is no limit on the number of requests.
TEST_F(ThriftConnectionManagerTest, RequestWithNoMaxRequestsLimit) {
  initializeFilter();
  EXPECT_EQ(0, config_->maxRequestsPerConnection());

  EXPECT_EQ(50, sendRequests(50));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, store_.counter("test.downstream_cx_max_requests").value());
  EXPECT_EQ(50U, store_.counter("test.request").value());
  EXPECT_EQ(50U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(50U, store_.counter("test.response").value());
  EXPECT_EQ(50U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(50U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
}

// Test the case where there is a limit on the number of requests but the actual number of requests
// does not reach the limit.
TEST_F(ThriftConnectionManagerTest, RequestWithMaxRequestsLimitButNotReach) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    route_config:
      name: local_route
    max_requests_per_connection: 50
    )EOF";

  initializeFilter(yaml);
  EXPECT_EQ(50, config_->maxRequestsPerConnection());

  EXPECT_EQ(49, sendRequests(49));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(0U, store_.counter("test.downstream_cx_max_requests").value());
  EXPECT_EQ(49U, store_.counter("test.request").value());
  EXPECT_EQ(49U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(49U, store_.counter("test.response").value());
  EXPECT_EQ(49U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(49U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
}

// Test the case where there is a limit on the number of requests and the actual number of requests
// happens to reach the limit.
TEST_F(ThriftConnectionManagerTest, RequestWithMaxRequestsLimitAndReached) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    route_config:
      name: local_route
    max_requests_per_connection: 50
    )EOF";

  initializeFilter(yaml);
  EXPECT_EQ(50, config_->maxRequestsPerConnection());

  // Since max requests per connection is set to 50, the connection will be disconnected after
  // all 50 requests is completed.
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  EXPECT_EQ(50, sendRequests(50));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.downstream_cx_max_requests").value());
  EXPECT_EQ(50U, store_.counter("test.request").value());
  EXPECT_EQ(50U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(50U, store_.counter("test.response").value());
  EXPECT_EQ(50U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(50U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
}

// Test the case where there is a limit on the number of requests and the actual number of requests
// exceeds the limit.
TEST_F(ThriftConnectionManagerTest, RequestWithMaxRequestsLimitAndReachedWithMoreRequests) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    route_config:
      name: local_route
    max_requests_per_connection: 50
    )EOF";

  initializeFilter(yaml);
  EXPECT_EQ(50, config_->maxRequestsPerConnection());

  // Since max requests per connection is set to 50, the connection will be disconnected after
  // all 50 requests is completed.
  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite));

  EXPECT_EQ(50, sendRequests(55));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.downstream_cx_max_requests").value());
  EXPECT_EQ(50U, store_.counter("test.request").value());
  EXPECT_EQ(50U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(50U, store_.counter("test.response").value());
  EXPECT_EQ(50U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(50U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
}

// Test cases where the number of requests is limited and the actual number of requests exceeds the
// limit several times.
TEST_F(ThriftConnectionManagerTest, RequestWithMaxRequestsLimitAndReachedRepeatedly) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    route_config:
      name: local_route
    max_requests_per_connection: 5
    )EOF";

  initializeFilter(yaml);
  EXPECT_EQ(5, config_->maxRequestsPerConnection());

  EXPECT_CALL(filter_callbacks_.connection_, close(Network::ConnectionCloseType::FlushWrite))
      .Times(5);

  auto mock_new_connection = [this]() {
    filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

    filter_ = nullptr;

    filter_callbacks_.connection_.read_enabled_ = true;
    filter_callbacks_.connection_.state_ = Network::Connection::State::Open;
    filter_callbacks_.connection_.callbacks_.clear();

    ON_CALL(random_, random()).WillByDefault(Return(42));
    filter_ = std::make_unique<ConnectionManager>(
        *config_, random_, filter_callbacks_.connection_.dispatcher_.timeSource(), drain_decision_);
    filter_->initializeReadFilterCallbacks(filter_callbacks_);
    filter_->onNewConnection();

    filter_->onAboveWriteBufferHighWatermark();
    filter_->onBelowWriteBufferLowWatermark();
  };

  EXPECT_EQ(5, sendRequests(6));

  for (size_t i = 0; i < 4; i++) {
    mock_new_connection();
    EXPECT_EQ(5, sendRequests(6));
  }

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(5U, store_.counter("test.downstream_cx_max_requests").value());
  EXPECT_EQ(25U, store_.counter("test.request").value());
  EXPECT_EQ(25U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(25U, store_.counter("test.response").value());
  EXPECT_EQ(25U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(25U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
}

TEST_F(ThriftConnectionManagerTest, DownstreamProtocolUpgrade) {
  custom_transport_ = new NiceMock<MockTransport>();
  custom_protocol_ = new NiceMock<MockProtocol>();
  initializeFilter();

  EXPECT_CALL(*custom_transport_, decodeFrameStart(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*custom_protocol_, readMessageBegin(_, _))
      .WillOnce(Invoke([&](Buffer::Instance&, MessageMetadata& metadata) -> bool {
        metadata.setSequenceId(0);
        metadata.setMessageType(MessageType::Call);
        metadata.setProtocolUpgradeMessage(true);
        return true;
      }));
  EXPECT_CALL(*custom_protocol_, supportsUpgrade()).Times(AnyNumber()).WillRepeatedly(Return(true));

  MockDecoderEventHandler* upgrade_decoder = new NiceMock<MockDecoderEventHandler>();
  EXPECT_CALL(*custom_protocol_, upgradeRequestDecoder())
      .WillOnce(Invoke([&]() -> DecoderEventHandlerSharedPtr {
        return DecoderEventHandlerSharedPtr{upgrade_decoder};
      }));
  EXPECT_CALL(*upgrade_decoder, messageBegin(_)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*custom_protocol_, readStructBegin(_, _)).WillOnce(Return(true));
  EXPECT_CALL(*upgrade_decoder, structBegin(_)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*custom_protocol_, readFieldBegin(_, _, _, _))
      .WillOnce(Invoke(
          [&](Buffer::Instance&, std::string&, FieldType& field_type, int16_t& field_id) -> bool {
            field_type = FieldType::Stop;
            field_id = 0;
            return true;
          }));
  EXPECT_CALL(*custom_protocol_, readStructEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*upgrade_decoder, structEnd()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*custom_protocol_, readMessageEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*upgrade_decoder, messageEnd()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*custom_transport_, decodeFrameEnd(_)).WillOnce(Return(true));
  EXPECT_CALL(*upgrade_decoder, transportEnd()).WillOnce(Return(FilterStatus::Continue));

  MockDirectResponse* direct_response = new NiceMock<MockDirectResponse>();

  EXPECT_CALL(*custom_protocol_, upgradeResponse(Ref(*upgrade_decoder)))
      .WillOnce(Invoke([&](const DecoderEventHandler&) -> DirectResponsePtr {
        return DirectResponsePtr{direct_response};
      }));

  EXPECT_CALL(*direct_response, encode(_, Ref(*custom_protocol_), _))
      .WillOnce(Invoke([&](MessageMetadata&, Protocol&,
                           Buffer::Instance& buffer) -> DirectResponse::ResponseType {
        buffer.add("response");
        return DirectResponse::ResponseType::SuccessReply;
      }));
  EXPECT_CALL(*custom_transport_, encodeFrame(_, _, _))
      .WillOnce(Invoke(
          [&](Buffer::Instance& buffer, const MessageMetadata&, Buffer::Instance& message) -> void {
            EXPECT_EQ("response", message.toString());
            buffer.add("transport-encoded response");
          }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ("transport-encoded response", buffer.toString());
      }));

  Buffer::OwnedImpl buffer;
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
}

// Tests multiple filters are invoked in the correct order.
TEST_F(ThriftConnectionManagerTest, OnDataHandlesThriftCallWithMultipleFilters) {
  initializeFilterWithCustomFilters();

  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  InSequence s;
  EXPECT_CALL(*custom_decoder_filter_, messageBegin(_)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*decoder_filter_, messageBegin(_)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*bidirectional_filter_, decodeMessageBegin(_))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*custom_decoder_filter_, messageEnd()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*decoder_filter_, messageEnd()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*bidirectional_filter_, decodeMessageEnd()).WillOnce(Return(FilterStatus::Continue));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(1U, stats_.request_active_.value());

  // Reverse order for encoder filters.
  EXPECT_CALL(*bidirectional_filter_, encodeMessageBegin(_))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*encoder_filter_, messageBegin(_)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*custom_encoder_filter_, messageBegin(_)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*bidirectional_filter_, encodeMessageEnd()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*encoder_filter_, messageEnd()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*custom_encoder_filter_, messageEnd()).WillOnce(Return(FilterStatus::Continue));

  writeFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x01);
  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
}

// Tests stop iteration/resume with multiple filters.
TEST_F(ThriftConnectionManagerTest, OnDataResumesWithNextFilter) {
  initializeFilterWithCustomFilters();

  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*custom_decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_));

  ThriftFilters::EncoderFilterCallbacks* encoder_callbacks{};
  EXPECT_CALL(*custom_encoder_filter_, setEncoderFilterCallbacks(_))
      .WillOnce(Invoke(
          [&](ThriftFilters::EncoderFilterCallbacks& cb) -> void { encoder_callbacks = &cb; }));

  // First decoder filter stops iteration.
  {
    EXPECT_CALL(*custom_decoder_filter_, messageBegin(_))
        .WillOnce(Return(FilterStatus::StopIteration));
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
    EXPECT_EQ(0U, store_.counter("test.request").value());
    EXPECT_EQ(1U, stats_.request_active_.value());
  }

  // Resume processing.
  {
    InSequence s;
    EXPECT_CALL(*decoder_filter_, messageBegin(_)).WillOnce(Return(FilterStatus::Continue));
    EXPECT_CALL(*bidirectional_filter_, decodeMessageBegin(_))
        .WillOnce(Return(FilterStatus::Continue));
    EXPECT_CALL(*custom_decoder_filter_, messageEnd()).WillOnce(Return(FilterStatus::Continue));
    EXPECT_CALL(*decoder_filter_, messageEnd()).WillOnce(Return(FilterStatus::Continue));
    EXPECT_CALL(*bidirectional_filter_, decodeMessageEnd())
        .WillOnce(Return(FilterStatus::Continue));
    callbacks->continueDecoding();
  }

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
}

// Tests stop iteration/resume with multiple filters when iteration is stopped during
// transportEnd.
TEST_F(ThriftConnectionManagerTest, OnDataResumesWithNextFilterOnTransportEnd) {
  initializeFilterWithCustomFilters();

  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*custom_decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_));

  // First filter stops iteration.
  {
    InSequence s;
    EXPECT_CALL(*custom_decoder_filter_, transportBegin(_))
        .WillOnce(Return(FilterStatus::Continue));
    EXPECT_CALL(*decoder_filter_, transportBegin(_)).WillOnce(Return(FilterStatus::Continue));
    EXPECT_CALL(*custom_decoder_filter_, transportEnd())
        .WillOnce(Return(FilterStatus::StopIteration));
    EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
    EXPECT_EQ(0U, store_.counter("test.request").value());
    EXPECT_EQ(1U, stats_.request_active_.value());
  }

  // Resume processing.
  {
    InSequence s;
    EXPECT_CALL(*decoder_filter_, transportEnd()).WillOnce(Return(FilterStatus::Continue));
    callbacks->continueDecoding();
  }

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
}

// Tests multiple filters where one invokes sendLocalReply with a successful reply.
TEST_F(ThriftConnectionManagerTest, OnDataWithFilterSendsLocalReply) {
  initializeFilterWithCustomFilters();

  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*custom_decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_));

  NiceMock<MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _))
      .WillOnce(Invoke([&](MessageMetadata&, Protocol&,
                           Buffer::Instance& buffer) -> DirectResponse::ResponseType {
        buffer.add("response");
        return DirectResponse::ResponseType::SuccessReply;
      }));
  {
    InSequence s;
    EXPECT_CALL(*custom_decoder_filter_, onLocalReply(_, _));
    EXPECT_CALL(*decoder_filter_, onLocalReply(_, _));
    EXPECT_CALL(*custom_encoder_filter_, onLocalReply(_, _));
    EXPECT_CALL(*encoder_filter_, onLocalReply(_, _));
    EXPECT_CALL(*bidirectional_filter_, onLocalReply(_, _));
  }

  // First filter sends local reply.
  EXPECT_CALL(*custom_decoder_filter_, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr) -> FilterStatus {
        callbacks->sendLocalReply(direct_response, false);
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(8, buffer.drainBEInt<int32_t>());
        EXPECT_EQ("response", buffer.toString());
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());

  EXPECT_EQ(access_log_data_,
            "name cluster passthrough_enabled=false framed binary call - - - - 0 0 0 -\n");
}

// Tests multiple filters where one invokes sendLocalReply with an error reply.
TEST_F(ThriftConnectionManagerTest, OnDataWithFilterSendsLocalErrorReply) {
  initializeFilterWithCustomFilters();

  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*custom_decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_));

  NiceMock<MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _))
      .WillOnce(Invoke([&](MessageMetadata&, Protocol&,
                           Buffer::Instance& buffer) -> DirectResponse::ResponseType {
        buffer.add("response");
        return DirectResponse::ResponseType::ErrorReply;
      }));

  {
    InSequence s;
    EXPECT_CALL(*custom_decoder_filter_, onLocalReply(_, _));
    EXPECT_CALL(*decoder_filter_, onLocalReply(_, _));
    EXPECT_CALL(*custom_encoder_filter_, onLocalReply(_, _));
    EXPECT_CALL(*encoder_filter_, onLocalReply(_, _));
    EXPECT_CALL(*bidirectional_filter_, onLocalReply(_, _));
  }

  // First filter sends local reply.
  EXPECT_CALL(*custom_decoder_filter_, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr) -> FilterStatus {
        callbacks->sendLocalReply(direct_response, false);
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> void {
        EXPECT_EQ(8, buffer.drainBEInt<int32_t>());
        EXPECT_EQ("response", buffer.toString());
      }));
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(1U, store_.counter("test.response_error").value());

  EXPECT_EQ(access_log_data_,
            "name cluster passthrough_enabled=false framed binary call - - - - 0 0 0 -\n");
}

// sendLocalReply does nothing, when the remote closed the connection.
TEST_F(ThriftConnectionManagerTest, OnDataWithFilterSendLocalReplyRemoteClosedConnection) {
  initializeFilterWithCustomFilters();

  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*custom_decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_));

  NiceMock<MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _)).Times(0);

  // First filter sends local reply.
  EXPECT_CALL(*custom_decoder_filter_, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr) -> FilterStatus {
        callbacks->sendLocalReply(direct_response, false);
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(filter_callbacks_.connection_, write(_, false)).Times(0);
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));

  // Remote closes the connection.
  filter_callbacks_.connection_.state_ = Network::Connection::State::Closed;
  EXPECT_EQ(filter_->onData(buffer_, true), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());

  EXPECT_EQ(access_log_data_,
            "name cluster passthrough_enabled=false framed binary call - - - - 0 0 0 -\n");
}

// Tests a decoder filter that modifies data.
TEST_F(ThriftConnectionManagerTest, DecoderFiltersModifyRequests) {
  initializeFilterWithCustomFilters();

  writeComplexFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*custom_decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_));

  Http::LowerCaseString key{"key"};

  EXPECT_CALL(*custom_decoder_filter_, transportBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_THAT(*metadata, HasNoRequestHeaders());
        metadata->requestHeaders().addCopy(key, "value");
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filter_, transportBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        const auto header = metadata->requestHeaders().get(key);
        EXPECT_FALSE(header.empty());
        EXPECT_EQ("value", header[0]->value().getStringView());
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*custom_decoder_filter_, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_EQ("name", metadata->methodName());
        metadata->setMethodName("alternate");
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filter_, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_EQ("alternate", metadata->methodName());
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*custom_decoder_filter_, boolValue(_))
      .WillOnce(Invoke([&](bool& value) -> FilterStatus {
        EXPECT_EQ(true, value);
        value = false;
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*decoder_filter_, boolValue(_)).WillOnce(Invoke([&](bool& value) -> FilterStatus {
    EXPECT_EQ(false, value);
    return FilterStatus::Continue;
  }));
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
}

// Tests a encoder filter that modifies data.
TEST_F(ThriftConnectionManagerTest, EncoderFiltersModifyRequests) {
  initializeFilterWithCustomFilters();

  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*custom_decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  Http::LowerCaseString key{"key"};

  EXPECT_CALL(*encoder_filter_, transportBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_THAT(*metadata, HasNoResponseHeaders());
        metadata->responseHeaders().addCopy(key, "value");
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*custom_encoder_filter_, transportBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        const auto header = metadata->responseHeaders().get(key);
        EXPECT_FALSE(header.empty());
        EXPECT_EQ("value", header[0]->value().getStringView());
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filter_, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_EQ("name", metadata->methodName());
        metadata->setMethodName("alternate");
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*custom_encoder_filter_, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr metadata) -> FilterStatus {
        EXPECT_EQ("alternate", metadata->methodName());
        return FilterStatus::Continue;
      }));
  EXPECT_CALL(*encoder_filter_, boolValue(_)).WillOnce(Invoke([&](bool& value) -> FilterStatus {
    EXPECT_EQ(true, value);
    value = false;
    return FilterStatus::Continue;
  }));
  EXPECT_CALL(*custom_encoder_filter_, boolValue(_))
      .WillOnce(Invoke([&](bool& value) -> FilterStatus {
        EXPECT_EQ(false, value);
        return FilterStatus::Continue;
      }));
  writeComplexFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x0F);
  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);
  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));
  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(1U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
}

TEST_F(ThriftConnectionManagerTest, TransportEndWhenRemoteClose) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.thrift_connection_draining", "true"}});

  // We want the Drain header to be set by RemoteClose which triggers end downstream in local reply.
  EXPECT_CALL(drain_decision_, drainClose()).WillOnce(Return(false));

  initializeFilter();
  writeComplexFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  writeComplexFramedBinaryMessage(write_buffer_, MessageType::Reply, 0x0F);

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  // Remote closes the connection.
  filter_callbacks_.connection_.state_ = Network::Connection::State::Closed;
  EXPECT_EQ(ThriftFilters::ResponseStatus::Reset, callbacks->upstreamData(write_buffer_));
  EXPECT_EQ(0U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_decoding_error").value());
  EXPECT_EQ(1U, store_.counter("test.downstream_response_drain_close").value());

  // Upstream connection is closed by remote. Hence we expect the downstream connection to be
  // closed after the response is sent. Drain header is set to hint downstream not to use the
  // connection.
  const auto header =
      callbacks->responseMetadata()->responseHeaders().get(ThriftProxy::Headers::get().Drain);
  EXPECT_FALSE(header.empty());
  EXPECT_EQ("true", header[0]->value().getStringView());

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(access_log_data_, "");
}

// TODO(caitong93): use TEST_P to avoid duplicating test cases
TEST_F(ThriftConnectionManagerTest, PayloadPassthroughOnDataHandlesThriftCall) {
  const std::string yaml = R"EOF(
transport: FRAMED
protocol: BINARY
stat_prefix: test
payload_passthrough: true
)EOF";

  initializeFilter(yaml);
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  // No response since the decoder filter stop the iteration.
  passthroughSupportedSetup(true, false);

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0, buffer_.length());

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(1U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());
}

TEST_F(ThriftConnectionManagerTest, PayloadPassthroughOnDataHandlesThriftOneWay) {
  const std::string yaml = fmt::format(R"EOF(
stat_prefix: test
payload_passthrough: true
{}
)EOF",
                                       accessLogConfig());

  initializeFilter(yaml);
  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 0x0F);

  // No response for oneway.
  passthroughSupportedSetup(true, false);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(0U, store_.counter("test.request_call").value());
  EXPECT_EQ(1U, store_.counter("test.request_oneway").value());
  EXPECT_EQ(0U, store_.counter("test.request_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.request_decoding_error").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(0U, store_.counter("test.response").value());

  EXPECT_EQ(access_log_data_,
            "name - passthrough_enabled=true framed binary oneway - - - - 0 0 0 -\n");
}

TEST_F(ThriftConnectionManagerTest, PayloadPassthroughRequestAndExceptionResponse) {
  const std::string yaml = fmt::format(R"EOF(
stat_prefix: test
payload_passthrough: true
{}
)EOF",
                                       accessLogConfig());

  initializeFilter(yaml);
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  passthroughSupportedSetup();

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  writeFramedBinaryTApplicationException(write_buffer_, 0x0F);

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());
  EXPECT_EQ(1U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());

  EXPECT_EQ(
      access_log_data_,
      "name - passthrough_enabled=true framed binary call framed binary exception - 0 0 0 -\n");
}

TEST_F(ThriftConnectionManagerTest, PayloadPassthroughRequestAndErrorResponse) {
  const std::string yaml = fmt::format(R"EOF(
stat_prefix: test
payload_passthrough: true
{}
)EOF",
                                       accessLogConfig());

  initializeFilter(yaml);
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  passthroughSupportedSetup();

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  writeFramedBinaryIDLException(write_buffer_, 0x0F);

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(1U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(0U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(1U, store_.counter("test.response_passthrough").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(1U, store_.counter("test.response_error").value());

  EXPECT_EQ(
      access_log_data_,
      "name - passthrough_enabled=true framed binary call framed binary reply error 0 0 0 -\n");
}

TEST_F(ThriftConnectionManagerTest, PayloadPassthroughRequestAndInvalidResponse) {
  const std::string yaml = fmt::format(R"EOF(
stat_prefix: test
payload_passthrough: true
{}
)EOF",
                                       accessLogConfig());

  initializeFilter(yaml);
  writeFramedBinaryMessage(buffer_, MessageType::Call, 0x0F);

  passthroughSupportedSetup();

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request_call").value());

  // Call is not valid in a response
  writeFramedBinaryMessage(write_buffer_, MessageType::Call, 0x0F);

  FramedTransportImpl transport;
  BinaryProtocolImpl proto;
  callbacks->startUpstreamResponse(transport, proto);

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  EXPECT_EQ(ThriftFilters::ResponseStatus::Complete, callbacks->upstreamData(write_buffer_));

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();

  EXPECT_EQ(1U, store_.counter("test.request").value());
  EXPECT_EQ(1U, store_.counter("test.request_call").value());
  EXPECT_EQ(0U, stats_.request_active_.value());
  EXPECT_EQ(1U, store_.counter("test.response").value());
  EXPECT_EQ(0U, store_.counter("test.response_reply").value());
  EXPECT_EQ(0U, store_.counter("test.response_exception").value());
  EXPECT_EQ(1U, store_.counter("test.response_invalid_type").value());
  EXPECT_EQ(0U, store_.counter("test.response_success").value());
  EXPECT_EQ(0U, store_.counter("test.response_error").value());

  EXPECT_EQ(access_log_data_,
            "name - passthrough_enabled=true framed binary call framed binary call - 0 0 0 -\n");
}

TEST_F(ThriftConnectionManagerTest, PayloadPassthroughRouting) {
  const std::string yaml = R"EOF(
transport: FRAMED
protocol: BINARY
payload_passthrough: true
stat_prefix: test
route_config:
  name: "routes"
  routes:
    - match:
        method_name: name
      route:
        cluster: cluster
)EOF";

  initializeFilter(yaml, {"cluster"});
  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 0x0F);

  // No response since the decoder filter stop the iteration.
  passthroughSupportedSetup(true, false);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));
  EXPECT_CALL(*decoder_filter_, messageBegin(_)).WillOnce(Return(FilterStatus::StopIteration));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(0U, store_.counter("test.request").value());
  EXPECT_EQ(1U, stats_.request_active_.value());

  Router::RouteConstSharedPtr route = callbacks->route();
  EXPECT_NE(nullptr, route);
  EXPECT_NE(nullptr, route->routeEntry());
  EXPECT_EQ("cluster", route->routeEntry()->clusterName());

  EXPECT_CALL(filter_callbacks_.connection_.dispatcher_, deferredDelete_(_));
  callbacks->continueDecoding();

  filter_callbacks_.connection_.dispatcher_.clearDeferredDeleteList();
}

// When a local reply was sent, payload passthrough is disabled because there's no
// active RPC left.
TEST_F(ThriftConnectionManagerTest, NoPayloadPassthroughOnLocalReply) {
  const std::string yaml = R"EOF(
transport: FRAMED
protocol: BINARY
payload_passthrough: true
stat_prefix: test
route_config:
  name: "routes"
  routes:
    - match:
        method_name: not_handled
      route:
        cluster: cluster
)EOF";

  initializeFilter(yaml, {"cluster"});
  writeFramedBinaryMessage(buffer_, MessageType::Oneway, 0x0F);

  // PassthroughData is not expected to be called.
  passthroughSupportedSetup(false, false);

  ThriftFilters::DecoderFilterCallbacks* callbacks{};
  EXPECT_CALL(*decoder_filter_, setDecoderFilterCallbacks(_))
      .WillOnce(
          Invoke([&](ThriftFilters::DecoderFilterCallbacks& cb) -> void { callbacks = &cb; }));

  NiceMock<MockDirectResponse> direct_response;
  EXPECT_CALL(direct_response, encode(_, _, _)).Times(0);

  EXPECT_CALL(*decoder_filter_, messageBegin(_))
      .WillOnce(Invoke([&](MessageMetadataSharedPtr) -> FilterStatus {
        callbacks->sendLocalReply(direct_response, false);
        return FilterStatus::StopIteration;
      }));

  EXPECT_EQ(filter_->onData(buffer_, false), Network::FilterStatus::StopIteration);
  EXPECT_EQ(1U, store_.counter("test.request").value());

  Router::RouteConstSharedPtr route = callbacks->route();
  EXPECT_EQ(nullptr, route);
}

TEST_F(ThriftConnectionManagerTest, UnknownCluster) {
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
        cluster: cluster1
    - match:
        method_name: name2
      route:
        cluster: cluster2
)EOF";

  EXPECT_THROW_WITH_REGEX(initializeFilter(yaml), EnvoyException, "unknown thrift cluster");
  EXPECT_THROW_WITH_REGEX(initializeFilter(yaml, {"cluster1"}), EnvoyException,
                          "unknown thrift cluster");
  EXPECT_THROW_WITH_REGEX(initializeFilter(yaml, {"cluster2"}), EnvoyException,
                          "unknown thrift cluster");
}

TEST_F(ThriftConnectionManagerTest, UnknownClusterWithValidateClustersDisabled) {
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
        cluster: cluster1
    - match:
        method_name: name2
      route:
        cluster: cluster2
  validate_clusters: false
)EOF";

  EXPECT_NO_THROW(initializeFilter(yaml));
}

TEST_F(ThriftConnectionManagerTest, UnknownWeightedCluster) {
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config;
  {
    auto* route_config = config.mutable_route_config();
    route_config->set_name("config");
    auto* route = route_config->add_routes();
    route->mutable_match()->set_method_name("foo");
    auto* action = route->mutable_route();
    auto* cluster1 = action->mutable_weighted_clusters()->add_clusters();
    cluster1->set_name("cluster1");
    cluster1->mutable_weight()->set_value(50);
    auto* cluster2 = action->mutable_weighted_clusters()->add_clusters();
    cluster2->set_name("cluster2");
    cluster2->mutable_weight()->set_value(50);
  }

  EXPECT_THROW_WITH_REGEX(initializeFilter(config), EnvoyException,
                          "unknown thrift weighted cluster");
  EXPECT_THROW_WITH_REGEX(initializeFilter(config, {"cluster1"}), EnvoyException,
                          "unknown thrift weighted cluster");
  EXPECT_THROW_WITH_REGEX(initializeFilter(config, {"cluster2"}), EnvoyException,
                          "unknown thrift weighted cluster");
}

TEST_F(ThriftConnectionManagerTest, UnknownWeightedClusterWithValidateClustersDisabled) {
  envoy::extensions::filters::network::thrift_proxy::v3::ThriftProxy config;
  {
    auto* route_config = config.mutable_route_config();
    route_config->set_name("config");
    auto* validate_clusters = route_config->mutable_validate_clusters();
    validate_clusters->set_value(false);
    auto* route = route_config->add_routes();
    route->mutable_match()->set_method_name("foo");
    auto* action = route->mutable_route();
    auto* cluster1 = action->mutable_weighted_clusters()->add_clusters();
    cluster1->set_name("cluster1");
    cluster1->mutable_weight()->set_value(50);
    auto* cluster2 = action->mutable_weighted_clusters()->add_clusters();
    cluster2->set_name("cluster2");
    cluster2->mutable_weight()->set_value(50);
  }

  EXPECT_NO_THROW(initializeFilter(config));
}

TEST_F(ThriftConnectionManagerTest, WeightedClusterNotSpecified) {
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
      - match:
          method_name: name2
        route:
          weighted_clusters:
            clusters:
              - weight: 2000
      )EOF";

  EXPECT_THROW_WITH_REGEX(initializeFilter(yaml, {"cluster"}), EnvoyException,
                          "Proto constraint validation failed");
}

TEST_F(ThriftConnectionManagerTest, UnknownMirrorPolicyCluster) {
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
        request_mirror_policies:
          - cluster: unknown_cluster
)EOF";
  // Intentionally miss "unknown_cluster" cluster.
  EXPECT_THROW_WITH_REGEX(initializeFilter(yaml, {"cluster"}), EnvoyException,
                          "unknown thrift shadow cluster");
}

TEST_F(ThriftConnectionManagerTest, UnknownMirrorPolicyClusterWithValidateClustersDisabled) {
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
        request_mirror_policies:
          - cluster: unknown_cluster
  validate_clusters: false
)EOF";
  EXPECT_NO_THROW(initializeFilter(yaml, {"cluster"}));
}

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
