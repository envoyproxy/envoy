#include <memory>

#include "envoy/common/exception.h"

#include "common/buffer/buffer_impl.h"

#include "extensions/filters/network/thrift_proxy/framed_transport_impl.h"
#include "extensions/filters/network/thrift_proxy/twitter_protocol_impl.h"

#include "test/extensions/filters/network/thrift_proxy/utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class TestTwitterProtocolImpl : public TwitterProtocolImpl {
public:
  void readRequestHeaderForTest(Buffer::Instance& buffer, MessageMetadata& metadata) {
    ThriftObjectPtr thrift_obj = newHeader();
    EXPECT_TRUE(thrift_obj->onData(buffer));
    updateMetadataWithRequestHeader(*thrift_obj, metadata);
  }
  void readResponseHeaderForTest(Buffer::Instance& buffer, MessageMetadata& metadata) {
    ThriftObjectPtr thrift_obj = newHeader();
    EXPECT_TRUE(thrift_obj->onData(buffer));
    updateMetadataWithResponseHeader(*thrift_obj, metadata);
  }
  void writeRequestHeaderForTest(Buffer::Instance& buffer, const MessageMetadata& metadata) {
    writeRequestHeader(buffer, metadata);
  }
  void writeResponseHeaderForTest(Buffer::Instance& buffer, const MessageMetadata& metadata) {
    writeResponseHeader(buffer, metadata);
  }
};

class TwitterProtocolTest : public testing::Test {
public:
  void clearMetadata() { metadata_ = std::make_shared<MessageMetadata>(); }

  void resetMetadata() {
    clearMetadata();
    metadata_->setMethodName("-");
    metadata_->setMessageType(MessageType::Oneway);
    metadata_->setSequenceId(1);
  }

  void expectMetadata(const std::string& name, MessageType msg_type, int32_t seq_id) {
    EXPECT_TRUE(metadata_->hasMethodName());
    EXPECT_EQ(name, metadata_->methodName());

    EXPECT_TRUE(metadata_->hasMessageType());
    EXPECT_EQ(msg_type, metadata_->messageType());

    EXPECT_TRUE(metadata_->hasSequenceId());
    EXPECT_EQ(seq_id, metadata_->sequenceId());

    EXPECT_FALSE(metadata_->hasFrameSize());
    EXPECT_FALSE(metadata_->hasProtocol());
    EXPECT_FALSE(metadata_->hasAppException());
    EXPECT_EQ(metadata_->headers().size(), 0);
  }

  void addMessageStart(Buffer::Instance& buffer, const std::string& name = "the_name",
                       MessageType msg_type = MessageType::Call, int32_t seq_id = 101) {
    buffer.writeBEInt<int16_t>(0x8001);
    buffer.writeByte(0);
    buffer.writeByte(msg_type);
    buffer.writeBEInt<int32_t>(name.length());
    buffer.add(name);
    buffer.writeBEInt<int32_t>(seq_id);
  }

  void addUpgradedMessageStart(Buffer::Instance& buffer, const std::string& name = "the_name",
                               MessageType msg_type = MessageType::Call, int32_t seq_id = 101,
                               int64_t trace_id = 1, int64_t span_id = 2,
                               const std::string& client_id = "test_client") {
    clearMetadata();

    TestTwitterProtocolImpl proto;
    metadata_->setTraceId(trace_id);
    metadata_->setSpanId(span_id);
    metadata_->headers().addCopy(Http::LowerCaseString(":client-id"), client_id);

    proto.writeRequestHeaderForTest(buffer, *metadata_);
    addMessageStart(buffer, name, msg_type, seq_id);

    clearMetadata();
  }

  void addUpgradedReplyStart(Buffer::Instance& buffer, const std::string& name = "the_name",
                             MessageType msg_type = MessageType::Reply, int32_t seq_id = 101,
                             int64_t trace_id = 1, int64_t span_id = 2) {
    clearMetadata();

    TestTwitterProtocolImpl proto;

    metadata_->mutable_spans().emplace_back(trace_id, "", span_id, absl::optional<int64_t>(),
                                            AnnotationList(), BinaryAnnotationList(), false);
    metadata_->headers().addCopy(Http::LowerCaseString("test-header"), "test-header-value");

    proto.writeResponseHeaderForTest(buffer, *metadata_);
    addMessageStart(buffer, name, msg_type, seq_id);

    clearMetadata();
  }

  void addUpgradeMessage(Buffer::Instance& buffer, int32_t seq_id = 100) {
    addMessageStart(buffer, TwitterProtocolImpl::upgradeMethodName(), MessageType::Call, seq_id);
  }

  // Mutates the given TwitterProtocolImpl to be in the upgraded state for processing request
  // (call/oneway) messages.
  void upgradeRequestProto(TwitterProtocolImpl& proto) {
    Buffer::OwnedImpl buffer;
    clearMetadata();

    addUpgradeMessage(buffer);
    buffer.writeByte(0); // empty connection options

    EXPECT_TRUE(proto.readMessageBegin(buffer, *metadata_));

    DecoderEventHandlerSharedPtr decoder = proto.upgradeRequestDecoder();
    EXPECT_NE(nullptr, decoder);

    std::string name;
    FieldType field_type;
    int16_t field_id;

    EXPECT_TRUE(proto.readStructBegin(buffer, name));
    EXPECT_EQ(FilterStatus::Continue, decoder->structBegin(name));

    EXPECT_TRUE(proto.readFieldBegin(buffer, name, field_type, field_id));
    EXPECT_EQ(FieldType::Stop, field_type);
    EXPECT_EQ(FilterStatus::Continue, decoder->fieldBegin(name, field_type, field_id));

    EXPECT_TRUE(proto.readStructEnd(buffer));
    EXPECT_EQ(FilterStatus::Continue, decoder->structEnd());

    EXPECT_TRUE(proto.readMessageEnd(buffer));
    EXPECT_EQ(FilterStatus::Continue, decoder->messageEnd());
    EXPECT_EQ(FilterStatus::Continue, decoder->transportEnd());
    EXPECT_EQ(buffer.length(), 0);

    DirectResponsePtr response = proto.upgradeResponse(*decoder);
    EXPECT_NE(nullptr, response);
    EXPECT_TRUE(proto.upgraded().has_value());
    EXPECT_TRUE(proto.upgraded().value());
  }

  // Mutates the given TwitterProtocolImpl to be in the upgraded state for processing response
  // (reply/exception) messages.
  void upgradeResponseProto(TwitterProtocolImpl& proto) {
    FramedTransportImpl transport;
    ThriftConnectionState conn_state;
    clearMetadata();

    ThriftObjectPtr response_decoder;
    {
      Buffer::OwnedImpl buffer;

      response_decoder = proto.attemptUpgrade(transport, conn_state, buffer);
      EXPECT_NE(nullptr, response_decoder);
    }

    {
      Buffer::OwnedImpl buffer;
      buffer.writeBEInt<int32_t>(TwitterProtocolImpl::upgradeMethodName().length() + 13);
      addSeq(buffer, {
                         0x80, 0x01, 0x00, 0x02, // binary, reply
                     });
      buffer.writeBEInt<int32_t>(TwitterProtocolImpl::upgradeMethodName().length());
      buffer.add(TwitterProtocolImpl::upgradeMethodName());
      buffer.writeBEInt<int32_t>(0);
      buffer.writeByte(0); // upgrade response stop field

      EXPECT_TRUE(response_decoder->onData(buffer));
    }

    proto.completeUpgrade(conn_state, *response_decoder);

    EXPECT_TRUE(conn_state.upgradeAttempted());
    EXPECT_TRUE(conn_state.isUpgraded());
    EXPECT_TRUE(proto.upgraded().has_value());
    EXPECT_TRUE(proto.upgraded().value());
  }

  MessageMetadataSharedPtr metadata_{new MessageMetadata()};
};

TEST_F(TwitterProtocolTest, Name) {
  TwitterProtocolImpl proto;
  EXPECT_EQ(proto.name(), "twitter");
}

TEST_F(TwitterProtocolTest, Type) {
  TwitterProtocolImpl proto;
  EXPECT_EQ(proto.type(), ProtocolType::Twitter);
}

// Tests readMessageBegin with insufficient data.
TEST_F(TwitterProtocolTest, ReadMessageBeginInsufficientData) {
  TwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  resetMetadata();

  addRepeated(buffer, 11, 'x');

  EXPECT_FALSE(proto.readMessageBegin(buffer, *metadata_));
  expectMetadata("-", MessageType::Oneway, 1);
  EXPECT_EQ(buffer.length(), 11);
  EXPECT_FALSE(proto.upgraded().has_value());
}

// Tests readMessageBegin when the initial message does not upgrade to twitter protocol.
TEST_F(TwitterProtocolTest, ReadMessageBeginNoUpgrade) {
  TwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  resetMetadata();

  addMessageStart(buffer);

  EXPECT_TRUE(proto.readMessageBegin(buffer, *metadata_));
  expectMetadata("the_name", MessageType::Call, 101);
  EXPECT_FALSE(metadata_->isProtocolUpgradeMessage());

  EXPECT_EQ(buffer.length(), 0);
  EXPECT_TRUE(proto.upgraded().has_value());
  EXPECT_FALSE(proto.upgraded().value());

  // Upgrade only works on first request attempt
  addMessageStart(buffer, proto.upgradeMethodName());

  EXPECT_TRUE(proto.readMessageBegin(buffer, *metadata_));
  EXPECT_EQ(proto.upgradeMethodName(), metadata_->methodName());
  EXPECT_FALSE(metadata_->isProtocolUpgradeMessage());
  EXPECT_TRUE(proto.upgraded().has_value());
  EXPECT_FALSE(proto.upgraded().value());
}

// Tests readMessageBegin when the initial message upgrades to twitter protocol
TEST_F(TwitterProtocolTest, ReadMessageBeginWithUpgrade) {
  TwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  resetMetadata();

  addUpgradeMessage(buffer);

  EXPECT_TRUE(proto.readMessageBegin(buffer, *metadata_));
  expectMetadata(TwitterProtocolImpl::upgradeMethodName(), MessageType::Call, 100);
  EXPECT_EQ(buffer.length(), 0);
  EXPECT_FALSE(proto.upgraded().has_value());
  EXPECT_TRUE(metadata_->isProtocolUpgradeMessage());
}

// Tests readMessageBegin/upgradeRequestDecoder/upgradeResponse sequence.
TEST_F(TwitterProtocolTest, RequestUpgradeSequence) {
  TwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  resetMetadata();

  addUpgradeMessage(buffer);
  buffer.writeByte(0); // empty connection options

  EXPECT_TRUE(proto.readMessageBegin(buffer, *metadata_));
  expectMetadata(TwitterProtocolImpl::upgradeMethodName(), MessageType::Call, 100);
  EXPECT_EQ(buffer.length(), 1);
  EXPECT_FALSE(proto.upgraded().has_value());
  EXPECT_TRUE(proto.supportsUpgrade());

  DecoderEventHandlerSharedPtr decoder = proto.upgradeRequestDecoder();
  EXPECT_NE(nullptr, decoder);

  std::string name;
  FieldType field_type;
  int16_t field_id;

  EXPECT_TRUE(proto.readStructBegin(buffer, name));
  EXPECT_EQ(FilterStatus::Continue, decoder->structBegin(name));

  EXPECT_TRUE(proto.readFieldBegin(buffer, name, field_type, field_id));
  EXPECT_EQ(FieldType::Stop, field_type);
  EXPECT_EQ(FilterStatus::Continue, decoder->fieldBegin(name, field_type, field_id));

  EXPECT_TRUE(proto.readStructEnd(buffer));
  EXPECT_EQ(FilterStatus::Continue, decoder->structEnd());

  EXPECT_TRUE(proto.readMessageEnd(buffer));
  EXPECT_EQ(FilterStatus::Continue, decoder->messageEnd());
  EXPECT_EQ(FilterStatus::Continue, decoder->transportEnd());
  EXPECT_EQ(buffer.length(), 0);

  DirectResponsePtr response = proto.upgradeResponse(*decoder);
  EXPECT_NE(nullptr, response);

  Buffer::OwnedImpl response_buffer;
  EXPECT_EQ(DirectResponse::ResponseType::SuccessReply,
            response->encode(*metadata_, proto, response_buffer));

  Buffer::OwnedImpl expected_buffer;
  addSeq(expected_buffer,
         {
             0x80,
             0x01,
             0x00,
             0x02, // binary, reply
             0x00,
             0x00,
             0x00,
             static_cast<uint8_t>(TwitterProtocolImpl::upgradeMethodName().length()),
         });
  expected_buffer.add(TwitterProtocolImpl::upgradeMethodName());
  addSeq(expected_buffer, {
                              0x00, 0x00, 0x00, 0x64, // sequence number
                              0x00,                   // upgrade response stop field
                          });
  EXPECT_EQ(expected_buffer.toString(), response_buffer.toString());

  EXPECT_TRUE(proto.upgraded().has_value());
  EXPECT_TRUE(proto.upgraded().value());
}

// Tests happy path attemptUpgrade/completeUpgrade sequence
TEST_F(TwitterProtocolTest, ResponseUpgradeSequence) {
  FramedTransportImpl transport;
  TwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  ThriftConnectionState conn_state;
  resetMetadata();

  EXPECT_TRUE(proto.supportsUpgrade());

  ThriftObjectPtr response_decoder = proto.attemptUpgrade(transport, conn_state, buffer);
  EXPECT_FALSE(conn_state.upgradeAttempted());
  EXPECT_FALSE(conn_state.isUpgraded());
  EXPECT_NE(nullptr, response_decoder);

  Buffer::OwnedImpl expected_buffer;
  expected_buffer.writeBEInt<int32_t>(TwitterProtocolImpl::upgradeMethodName().length() + 13);
  addSeq(expected_buffer, {
                              0x80, 0x01, 0x00, 0x01, // binary, call
                          });
  expected_buffer.writeBEInt<int32_t>(TwitterProtocolImpl::upgradeMethodName().length());
  expected_buffer.add(TwitterProtocolImpl::upgradeMethodName());
  expected_buffer.writeBEInt<int32_t>(0);
  expected_buffer.writeByte(0); // connection options stop field
  EXPECT_EQ(expected_buffer.toString(), buffer.toString());

  Buffer::OwnedImpl response_buffer;
  response_buffer.writeBEInt<int32_t>(TwitterProtocolImpl::upgradeMethodName().length() + 13);
  addSeq(response_buffer, {
                              0x80, 0x01, 0x00, 0x02, // binary, reply
                          });
  response_buffer.writeBEInt<int32_t>(TwitterProtocolImpl::upgradeMethodName().length());
  response_buffer.add(TwitterProtocolImpl::upgradeMethodName());
  response_buffer.writeBEInt<int32_t>(0);
  response_buffer.writeByte(0); // upgrade response stop field

  EXPECT_TRUE(response_decoder->onData(response_buffer));

  EXPECT_FALSE(conn_state.upgradeAttempted());
  EXPECT_FALSE(conn_state.isUpgraded());
  EXPECT_FALSE(proto.upgraded().has_value());

  proto.completeUpgrade(conn_state, *response_decoder);

  EXPECT_TRUE(conn_state.upgradeAttempted());
  EXPECT_TRUE(conn_state.isUpgraded());
  EXPECT_TRUE(proto.upgraded().has_value());
  EXPECT_TRUE(proto.upgraded().value());

  // Test that a subsequent upgrade attempt is skipped (since we've already upgraded)
  {
    TwitterProtocolImpl proto2;
    EXPECT_FALSE(proto2.upgraded().has_value());

    EXPECT_EQ(nullptr, proto2.attemptUpgrade(transport, conn_state, buffer));
    EXPECT_TRUE(proto2.upgraded().has_value());
    EXPECT_TRUE(proto2.upgraded().value());
  }
}

// Tests rejection of the attemptUpgrade/completeUpgrade sequence
TEST_F(TwitterProtocolTest, ResponseUpgradeRejectedSequence) {
  FramedTransportImpl transport;
  TwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  ThriftConnectionState conn_state;
  resetMetadata();

  EXPECT_TRUE(proto.supportsUpgrade());

  ThriftObjectPtr response_decoder = proto.attemptUpgrade(transport, conn_state, buffer);
  EXPECT_FALSE(conn_state.upgradeAttempted());
  EXPECT_FALSE(conn_state.isUpgraded());
  EXPECT_NE(nullptr, response_decoder);
  EXPECT_NE(0, buffer.length());

  Buffer::OwnedImpl response_buffer;
  std::string response_err =
      fmt::format("Unknown function {}", TwitterProtocolImpl::upgradeMethodName());
  response_buffer.writeBEInt<int32_t>(TwitterProtocolImpl::upgradeMethodName().length() +
                                      response_err.length() + 27);
  addSeq(response_buffer, {
                              0x80, 0x01, 0x00, 0x03, // binary, exception
                          });
  response_buffer.writeBEInt<int32_t>(TwitterProtocolImpl::upgradeMethodName().length());
  response_buffer.add(TwitterProtocolImpl::upgradeMethodName());
  response_buffer.writeBEInt<int32_t>(0);
  addSeq(response_buffer, {
                              0x0B, 0x00, 0x01, // string field 1
                          });
  response_buffer.writeBEInt<int32_t>(response_err.length());
  response_buffer.add(response_err);
  addSeq(response_buffer,
         {
             0x08, 0x00, 0x02, // int field 2
             0x00, 0x00, 0x00, static_cast<uint8_t>(AppExceptionType::UnknownMethod),
             0x00, // stop field
         });

  EXPECT_TRUE(response_decoder->onData(response_buffer));

  EXPECT_FALSE(conn_state.upgradeAttempted());
  EXPECT_FALSE(conn_state.isUpgraded());
  EXPECT_FALSE(proto.upgraded().has_value());

  proto.completeUpgrade(conn_state, *response_decoder);

  EXPECT_TRUE(conn_state.upgradeAttempted());
  EXPECT_FALSE(conn_state.isUpgraded());
  EXPECT_TRUE(proto.upgraded().has_value());
  EXPECT_FALSE(proto.upgraded().value());
}

// Tests parsing a RequestHeader
TEST_F(TwitterProtocolTest, ParseRequestHeader) {
  TestTwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  addSeq(buffer,
         {
             0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // trace_id
             0x0A, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, // span_id
             0x0A, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, // parent_span_id
             0x02, 0x00, 0x05, 0x01,                                           // sampled
             0x0C, 0x00, 0x06,                                                 // client-id struct
             0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10,                         // string, length 16
             0x74, 0x68, 0x72, 0x69, 0x66, 0x74, 0x2D, 0x63, 0x6C, 0x69, 0x65,
             0x6E, 0x74, 0x2D, 0x69, 0x64,
             0x00,                                                             // stop client-id
             0x0A, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, // flags
             0x0F, 0x00, 0x08, 0x0C, 0x00, 0x00, 0x00, 0x02,                   // contexts, size 2
             0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x6B, 0x31,             // key string
             0x0B, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x76, 0x31,             // value string
             0x00,                                                             // stop context 1
             0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x6B, 0x32,             // key string
             0x0B, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x76, 0x32,             // value string
             0x00,                                                             // stop context 2
             0x0B, 0x00, 0x09, 0x00, 0x00, 0x00, 0x04, 0x64, 0x65, 0x73, 0x74, // dest
             0x0F, 0x00, 0x0A, 0x0C, 0x00, 0x00, 0x00, 0x02,                   // delegations (2)
             0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x73, 0x31,             // src string
             0x0B, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x64, 0x31,             // dst string
             0x00,                                                             // stop delegation 1
             0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x73, 0x32,             // src string
             0x0B, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x64, 0x32,             // dst string
             0x00,                                                             // stop delegation 2
             0x0A, 0x00, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, // trace_id_high
             0x00,                                                             // stop
         });

  proto.readRequestHeaderForTest(buffer, *metadata_);

  EXPECT_TRUE(metadata_->traceId());
  EXPECT_EQ(1, *metadata_->traceId());
  EXPECT_TRUE(metadata_->traceIdHigh());
  EXPECT_EQ(2, *metadata_->traceIdHigh());
  EXPECT_TRUE(metadata_->spanId());
  EXPECT_EQ(100, *metadata_->spanId());
  EXPECT_TRUE(metadata_->parentSpanId());
  EXPECT_EQ(10, *metadata_->parentSpanId());
  EXPECT_TRUE(metadata_->sampled().has_value());
  EXPECT_TRUE(metadata_->sampled());
  EXPECT_TRUE(metadata_->flags());
  EXPECT_EQ(5, *metadata_->flags());

  Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
  EXPECT_EQ(6, test_headers.size());

  EXPECT_EQ("thrift-client-id", test_headers.get_(":client-id"));
  EXPECT_EQ("dest", test_headers.get_(":dest"));

  // Delegations
  EXPECT_EQ("d1", test_headers.get_(":d:s1"));
  EXPECT_EQ("d2", test_headers.get_(":d:s2"));

  // Contexts
  EXPECT_EQ("v1", test_headers.get_("k1"));
  EXPECT_EQ("v2", test_headers.get_("k2"));
}

// Tests parsing an empty RequestHeader
TEST_F(TwitterProtocolTest, ParseEmptyRequestHeader) {
  TestTwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {
                     0x00,
                 });

  proto.readRequestHeaderForTest(buffer, *metadata_);

  // trace_id and span_id are not optional fields, so they get default values when missing
  EXPECT_TRUE(metadata_->traceId());
  EXPECT_EQ(0, *metadata_->traceId());
  EXPECT_FALSE(metadata_->traceIdHigh());
  EXPECT_TRUE(metadata_->spanId());
  EXPECT_EQ(0, *metadata_->spanId());
  EXPECT_FALSE(metadata_->parentSpanId());
  EXPECT_FALSE(metadata_->sampled().has_value());
  EXPECT_FALSE(metadata_->flags());
  EXPECT_TRUE(metadata_->spans().empty());

  Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
  EXPECT_EQ(0, test_headers.size());
}

// Test writing a RequestHeader
TEST_F(TwitterProtocolTest, WriteRequestHeader) {
  metadata_->setTraceId(1);
  metadata_->setTraceIdHigh(2);
  metadata_->setSpanId(100);
  metadata_->setParentSpanId(10);
  metadata_->setSampled(true);
  metadata_->setFlags(5);
  Http::HeaderMap& headers = metadata_->headers();
  headers.addCopy(Http::LowerCaseString(":client-id"), "thrift-client-id");
  headers.addCopy(Http::LowerCaseString(":dest"), "dest");
  headers.addCopy(Http::LowerCaseString(":d:s1"), "d1");
  headers.addCopy(Http::LowerCaseString("key"), "value");
  headers.addCopy(Http::LowerCaseString(""), "value"); // ignored

  TestTwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  proto.writeRequestHeaderForTest(buffer, *metadata_);
  clearMetadata();

  proto.readRequestHeaderForTest(buffer, *metadata_);

  EXPECT_EQ(1, *metadata_->traceId());
  EXPECT_EQ(2, *metadata_->traceIdHigh());
  EXPECT_EQ(100, *metadata_->spanId());
  EXPECT_EQ(10, *metadata_->parentSpanId());
  EXPECT_TRUE(*metadata_->sampled());
  EXPECT_EQ(5, *metadata_->flags());

  Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
  EXPECT_EQ(4, test_headers.size());
  EXPECT_EQ("thrift-client-id", test_headers.get_(":client-id"));
  EXPECT_EQ("dest", test_headers.get_(":dest"));
  EXPECT_EQ("d1", test_headers.get_(":d:s1"));
  EXPECT_EQ("value", test_headers.get_("key"));
}

// Test writing a mostly empty RequestHeader
TEST_F(TwitterProtocolTest, WriteMostlyEmptyRequestHeader) {
  TestTwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  proto.writeRequestHeaderForTest(buffer, *metadata_);
  clearMetadata();

  proto.readRequestHeaderForTest(buffer, *metadata_);

  EXPECT_EQ(0, *metadata_->traceId());
  EXPECT_EQ(0, *metadata_->spanId());

  EXPECT_FALSE(metadata_->traceIdHigh());
  EXPECT_FALSE(metadata_->parentSpanId());
  EXPECT_FALSE(metadata_->sampled());
  EXPECT_FALSE(metadata_->flags());

  Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
  EXPECT_EQ(0, test_headers.size());
}

// Tests parsing of ResponseHeader structs
TEST_F(TwitterProtocolTest, ParseResponseHeader) {
  TestTwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  addSeq(buffer,
         {
             0x0F, 0x00, 0x01, 0x0C, 0x00, 0x00, 0x00, 0x02,                   // spans list, size 2
             0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, // trace_id
             0x0B, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x73, 0x31,             // name
             0x0A, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, // span_id
             0x0A, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, // parent_span_id
             0x0F, 0x00, 0x06, 0x0C, 0x00, 0x00, 0x00, 0x02,                   // annotations (2)
             0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x86, 0xA0, // timestamp
             0x0B, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x61, 0x31,             // value
             0x00,                                                             // stop annotation 1
             0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x0D, 0x40, // timestamp
             0x0B, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x61, 0x32,             // value
             0x0C, 0x00, 0x03,                                                 // endpoint struct
             0x08, 0x00, 0x01, 0xC0, 0xA8, 0x00, 0x01,                         // ipv4
             0x06, 0x00, 0x02, 0x1F, 0x40,                                     // port
             0x0B, 0x00, 0x03, 0x00, 0x00, 0x00, 0x07, 0x73, 0x65, 0x72, 0x76, // service_name
             0x69, 0x63, 0x65,
             0x00,                                                             // stop endpoint
             0x00,                                                             // stop annotation 2
             0x0F, 0x00, 0x08, 0x0C, 0x00, 0x00, 0x00, 0x02,                   // bin anno (2)
             0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x62, 0x61, 0x6B, 0x31, // key
             0x0B, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x62, 0x61, 0x76, 0x31, // value
             0x08, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06,                         // annotation_type
             0x00,                                                             // stop bi anno 1
             0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x62, 0x61, 0x6B, 0x32, // key
             0x0B, 0x00, 0x02, 0x00, 0x00, 0x00, 0x04, 0x62, 0x61, 0x76, 0x32, // value
             0x08, 0x00, 0x03, 0x00, 0x00, 0x00, 0x06,                         // annotation_type
             0x0C, 0x00, 0x04,                                                 // endpoint struct
             0x08, 0x00, 0x01, 0xC0, 0xA8, 0x00, 0x02,                         // ipv4
             0x06, 0x00, 0x02, 0x23, 0x28,                                     // port
             0x00,                                                             // stop endpoint
             0x00,                                                             // stop bin anno 2
             0x02, 0x00, 0x09, 0x01,                                           // debug
             0x00,                                                             // stop span 1
             0x0A, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, // trace_id
             0x0B, 0x00, 0x03, 0x00, 0x00, 0x00, 0x02, 0x73, 0x32,             // name
             0x0A, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC8, // span_id
             0x02, 0x00, 0x09, 0x00,                                           // debug
             0x00,                                                             // stop span 2
             0x0F, 0x00, 0x02, 0x0C, 0x00, 0x00, 0x00, 0x02,                   // contexts, size 2
             0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x6B, 0x31,             // key
             0x0B, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x76, 0x31,             // value
             0x00,                                                             // stop context 1
             0x0B, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x6B, 0x32,             // key
             0x0B, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02, 0x76, 0x32,             // value
             0x00,                                                             // stop context 2
             0x00,                                                             // stop span 2
         });

  clearMetadata();
  proto.readResponseHeaderForTest(buffer, *metadata_);

  EXPECT_EQ(2, metadata_->spans().size());
  {
    const Span& span = metadata_->spans().front();
    EXPECT_EQ(1, span.trace_id_);
    EXPECT_EQ("s1", span.name_);
    EXPECT_EQ(100, span.span_id_);
    EXPECT_TRUE(span.parent_span_id_);
    EXPECT_EQ(10, *span.parent_span_id_);
    EXPECT_EQ(2, span.annotations_.size());
    {
      const Annotation& anno = span.annotations_.front();
      EXPECT_EQ(100000, anno.timestamp_);
      EXPECT_EQ("a1", anno.value_);
      EXPECT_FALSE(anno.host_);
    }
    {
      const Annotation& anno = span.annotations_.back();
      EXPECT_EQ(200000, anno.timestamp_);
      EXPECT_EQ("a2", anno.value_);
      EXPECT_TRUE(anno.host_);
      EXPECT_EQ(0xC0A80001, static_cast<uint32_t>(anno.host_->ipv4_)); // 192.168.0.1
      EXPECT_EQ(8000, anno.host_->port_);
      EXPECT_EQ("service", anno.host_->service_name_);
    }
    EXPECT_EQ(2, span.binary_annotations_.size());
    {
      const BinaryAnnotation& anno = span.binary_annotations_.front();
      EXPECT_EQ("bak1", anno.key_);
      EXPECT_EQ("bav1", anno.value_);
      EXPECT_EQ(AnnotationType::String, anno.annotation_type_);
    }
    {
      const BinaryAnnotation& anno = span.binary_annotations_.back();
      EXPECT_EQ("bak2", anno.key_);
      EXPECT_EQ("bav2", anno.value_);
      EXPECT_EQ(AnnotationType::String, anno.annotation_type_);
      EXPECT_TRUE(anno.host_);
      EXPECT_EQ(0xC0A80002, static_cast<uint32_t>(anno.host_->ipv4_)); // 192.168.0.2
      EXPECT_EQ(9000, anno.host_->port_);
      EXPECT_EQ("", anno.host_->service_name_);
    }
    EXPECT_TRUE(span.debug_);
  }
  {
    const Span& span = metadata_->spans().back();
    EXPECT_EQ(2, span.trace_id_);
    EXPECT_EQ("s2", span.name_);
    EXPECT_EQ(200, span.span_id_);
    EXPECT_FALSE(span.parent_span_id_);
    EXPECT_TRUE(span.annotations_.empty());
    EXPECT_TRUE(span.binary_annotations_.empty());
    EXPECT_FALSE(span.debug_);
  }

  Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
  EXPECT_EQ(2, test_headers.size());
  EXPECT_EQ("v1", test_headers.get_("k1"));
  EXPECT_EQ("v2", test_headers.get_("k2"));
}

// Tests parsing of an empty ResponseHeader struct
TEST_F(TwitterProtocolTest, ParseEmptyResponseHeader) {
  TestTwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  addSeq(buffer, {
                     0x00,
                 });

  proto.readResponseHeaderForTest(buffer, *metadata_);

  EXPECT_TRUE(metadata_->spans().empty());

  Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
  EXPECT_EQ(0, test_headers.size());
}

// Test writing a ResponseHeader
TEST_F(TwitterProtocolTest, WriteResponseHeader) {
  Http::HeaderMap& headers = metadata_->headers();
  headers.addCopy(Http::LowerCaseString("key1"), "value1");
  headers.addCopy(Http::LowerCaseString("key2"), "value2");

  SpanList& spans = metadata_->mutable_spans();
  spans.emplace_back(1, "s1", 100, absl::optional<int64_t>(10),
                     AnnotationList({
                         Annotation(100000, "a1", {Endpoint(0xC0A80001, 0, "")}),
                         Annotation(100001, "a2", {}),
                     }),
                     BinaryAnnotationList({
                         BinaryAnnotation("bak1", "bav1", AnnotationType::I32,
                                          {
                                              Endpoint(0xC0A80002, 80, "service_name"),
                                          }),
                         BinaryAnnotation("bak2", "bav2", AnnotationType::String, {}),
                     }),
                     true);
  spans.emplace_back(2, "s2", 200, absl::optional<int64_t>(), AnnotationList(),
                     BinaryAnnotationList(), false);
  TestTwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  proto.writeResponseHeaderForTest(buffer, *metadata_);

  clearMetadata();
  proto.readResponseHeaderForTest(buffer, *metadata_);

  EXPECT_FALSE(metadata_->traceId());
  EXPECT_FALSE(metadata_->traceIdHigh());
  EXPECT_FALSE(metadata_->spanId());
  EXPECT_FALSE(metadata_->parentSpanId());
  EXPECT_FALSE(metadata_->sampled());
  EXPECT_FALSE(metadata_->flags());

  const SpanList& read_spans = metadata_->spans();
  EXPECT_EQ(2, read_spans.size());

  const Span& span1 = read_spans.front();
  EXPECT_EQ(1, span1.trace_id_);
  EXPECT_EQ("s1", span1.name_);
  EXPECT_EQ(100, span1.span_id_);
  EXPECT_EQ(10, *span1.parent_span_id_);
  EXPECT_EQ(2, span1.annotations_.size());
  const Annotation& anno1 = span1.annotations_.front();
  EXPECT_EQ(100000, anno1.timestamp_);
  EXPECT_EQ("a1", anno1.value_);
  EXPECT_TRUE(anno1.host_);
  EXPECT_EQ(0xC0A80001, anno1.host_->ipv4_);
  EXPECT_EQ(0, anno1.host_->port_);
  EXPECT_EQ("", anno1.host_->service_name_);
  EXPECT_EQ(2, span1.binary_annotations_.size());
  const Annotation& anno2 = span1.annotations_.back();
  EXPECT_EQ(100001, anno2.timestamp_);
  EXPECT_EQ("a2", anno2.value_);
  EXPECT_FALSE(anno2.host_);
  const BinaryAnnotation& bin_anno1 = span1.binary_annotations_.front();
  EXPECT_EQ("bak1", bin_anno1.key_);
  EXPECT_EQ("bav1", bin_anno1.value_);
  EXPECT_EQ(AnnotationType::I32, bin_anno1.annotation_type_);
  EXPECT_TRUE(bin_anno1.host_);
  EXPECT_EQ(0xC0A80002, bin_anno1.host_->ipv4_);
  EXPECT_EQ(80, bin_anno1.host_->port_);
  EXPECT_EQ("service_name", bin_anno1.host_->service_name_);
  const BinaryAnnotation& bin_anno2 = span1.binary_annotations_.back();
  EXPECT_EQ("bak2", bin_anno2.key_);
  EXPECT_EQ("bav2", bin_anno2.value_);
  EXPECT_EQ(AnnotationType::String, bin_anno2.annotation_type_);
  EXPECT_FALSE(bin_anno2.host_);

  const Span& span2 = read_spans.back();
  EXPECT_EQ(2, span2.trace_id_);
  EXPECT_EQ("s2", span2.name_);
  EXPECT_EQ(200, span2.span_id_);
  EXPECT_FALSE(span2.parent_span_id_);
  EXPECT_TRUE(span2.annotations_.empty());
  EXPECT_TRUE(span2.binary_annotations_.empty());
  EXPECT_FALSE(span2.debug_);

  Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
  EXPECT_EQ("value1", test_headers.get_("key1"));
  EXPECT_EQ("value2", test_headers.get_("key2"));
}

// Test writing an empty ResponseHeader
TEST_F(TwitterProtocolTest, WriteEmptyResponseHeader) {
  MessageMetadata metadata;
  TestTwitterProtocolImpl proto;
  Buffer::OwnedImpl buffer;
  proto.writeResponseHeaderForTest(buffer, *metadata_);

  clearMetadata();
  proto.readResponseHeaderForTest(buffer, *metadata_);

  EXPECT_FALSE(metadata_->traceId());
  EXPECT_FALSE(metadata_->traceIdHigh());
  EXPECT_FALSE(metadata_->spanId());
  EXPECT_FALSE(metadata_->parentSpanId());
  EXPECT_FALSE(metadata_->sampled());
  EXPECT_FALSE(metadata_->flags());

  EXPECT_TRUE(metadata_->spans().empty());

  Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
  EXPECT_EQ(0, test_headers.size());
}

TEST_F(TwitterProtocolTest, TestUpgradedRequestMessageBegin) {
  TwitterProtocolImpl proto;
  upgradeRequestProto(proto);

  Buffer::OwnedImpl buffer;
  addUpgradedMessageStart(buffer);

  MessageMetadata metadata;
  EXPECT_TRUE(proto.readMessageBegin(buffer, *metadata_));
  EXPECT_EQ("the_name", metadata_->methodName());
  EXPECT_EQ(MessageType::Call, metadata_->messageType());
  EXPECT_EQ(101, metadata_->sequenceId());
  EXPECT_EQ(1, *metadata_->traceId());
  EXPECT_EQ(2, *metadata_->spanId());
  Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
  EXPECT_EQ("test_client", test_headers.get_(":client-id"));
}

TEST_F(TwitterProtocolTest, TestUpgradedRequestMessageContinuation) {
  Buffer::OwnedImpl buffer;
  addUpgradedMessageStart(buffer);

  for (uint64_t split = 1; split < buffer.length() - 1; split++) {
    TwitterProtocolImpl proto;
    upgradeRequestProto(proto);

    Buffer::OwnedImpl partial_buffer;
    uint8_t* data = static_cast<uint8_t*>(buffer.linearize(buffer.length()));
    partial_buffer.add(data, split);
    EXPECT_FALSE(proto.readMessageBegin(partial_buffer, *metadata_));

    partial_buffer.add(data + split, buffer.length() - split);
    EXPECT_TRUE(proto.readMessageBegin(partial_buffer, *metadata_));

    EXPECT_EQ("the_name", metadata_->methodName());
    EXPECT_EQ(MessageType::Call, metadata_->messageType());
    EXPECT_EQ(101, metadata_->sequenceId());
    EXPECT_EQ(1, *metadata_->traceId());
    EXPECT_EQ(2, *metadata_->spanId());
    Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
    EXPECT_EQ("test_client", test_headers.get_(":client-id"));
  }
}

TEST_F(TwitterProtocolTest, TestUpgradedReplyMessageBegin) {
  TwitterProtocolImpl proto;
  upgradeResponseProto(proto);

  Buffer::OwnedImpl buffer;
  addUpgradedReplyStart(buffer);

  EXPECT_TRUE(proto.readMessageBegin(buffer, *metadata_));
  EXPECT_EQ("the_name", metadata_->methodName());
  EXPECT_EQ(MessageType::Reply, metadata_->messageType());
  EXPECT_EQ(101, metadata_->sequenceId());

  EXPECT_EQ(1, metadata_->spans().size());
  EXPECT_EQ(1, metadata_->spans().front().trace_id_);
  EXPECT_EQ(2, metadata_->spans().front().span_id_);
  Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
  EXPECT_EQ("test-header-value", test_headers.get_("test-header"));
}

TEST_F(TwitterProtocolTest, TestUpgradedReplyMessageContinuation) {
  Buffer::OwnedImpl buffer;
  addUpgradedReplyStart(buffer);

  for (uint64_t split = 1; split < buffer.length() - 1; split++) {
    TwitterProtocolImpl proto;
    upgradeResponseProto(proto);

    Buffer::OwnedImpl partial_buffer;
    uint8_t* data = static_cast<uint8_t*>(buffer.linearize(buffer.length()));
    partial_buffer.add(data, split);
    EXPECT_FALSE(proto.readMessageBegin(partial_buffer, *metadata_));

    partial_buffer.add(data + split, buffer.length() - split);
    EXPECT_TRUE(proto.readMessageBegin(partial_buffer, *metadata_));

    EXPECT_EQ("the_name", metadata_->methodName());
    EXPECT_EQ(MessageType::Reply, metadata_->messageType());
    EXPECT_EQ(101, metadata_->sequenceId());

    EXPECT_EQ(1, metadata_->spans().size());
    EXPECT_EQ(1, metadata_->spans().front().trace_id_);
    EXPECT_EQ(2, metadata_->spans().front().span_id_);
    Http::TestRequestHeaderMapImpl test_headers(metadata_->headers());
    EXPECT_EQ("test-header-value", test_headers.get_("test-header"));
  }
}

TEST_F(TwitterProtocolTest, TestUpgradedWriteMessageBegin) {
  TwitterProtocolImpl proto;
  upgradeRequestProto(proto);

  metadata_->setMethodName("message");
  metadata_->setSequenceId(1);
  metadata_->setTraceId(1);
  metadata_->mutable_spans().emplace_back(100, "", 100, absl::optional<int64_t>(), AnnotationList(),
                                          BinaryAnnotationList(), false);

  {
    // Call
    Buffer::OwnedImpl buffer;
    metadata_->setMessageType(MessageType::Call);
    proto.writeMessageBegin(buffer, *metadata_);

    EXPECT_EQ(std::string("\xA\0\x1\0\0\0\0\0\0\0\x1" // trace_id
                          "\xA\0\x2\0\0\0\0\0\0\0\0"  // span_id
                          "\0"                        // end request header
                          "\x80\x1\0\x1\0\0\0\x7message\0\0\0\x1",
                          42),
              buffer.toString());
  }
  {
    // Oneway
    Buffer::OwnedImpl buffer;
    metadata_->setMessageType(MessageType::Oneway);
    proto.writeMessageBegin(buffer, *metadata_);

    EXPECT_EQ(std::string("\xA\0\x1\0\0\0\0\0\0\0\x1" // trace_id
                          "\xA\0\x2\0\0\0\0\0\0\0\0"  // span_id
                          "\0"                        // end request header
                          "\x80\x1\0\x4\0\0\0\x7message\0\0\0\x1",
                          42),
              buffer.toString());
  }

  {
    // Reply
    Buffer::OwnedImpl buffer;
    metadata_->setMessageType(MessageType::Reply);
    proto.writeMessageBegin(buffer, *metadata_);

    EXPECT_EQ(std::string("\xF\0\x1\xC\0\0\0\x1"       // spans
                          "\xA\0\x1\0\0\0\0\0\0\0\x64" // span: trace_id
                          "\xB\0\x3\0\0\0\0"           // span: name
                          "\xA\0\x4\0\0\0\0\0\0\0\x64" // span: id
                          "\xF\0\x6\xC\0\0\0\0"        // span: annotations
                          "\xF\0\x8\xC\0\0\0\0"        // span: binary_annotations
                          "\x2\0\x9\0"                 // span: debug
                          "\0"                         // end span
                          "\0"                         // end response header
                          "\x80\x1\0\x2\0\0\0\x7message\0\0\0\x1",
                          78),
              buffer.toString());
  }

  {
    // Exception
    Buffer::OwnedImpl buffer;
    metadata_->setMessageType(MessageType::Exception);
    proto.writeMessageBegin(buffer, *metadata_);

    EXPECT_EQ(std::string("\xF\0\x1\xC\0\0\0\x1"       // spans
                          "\xA\0\x1\0\0\0\0\0\0\0\x64" // span: trace_id
                          "\xB\0\x3\0\0\0\0"           // span: name
                          "\xA\0\x4\0\0\0\0\0\0\0\x64" // span: id
                          "\xF\0\x6\xC\0\0\0\0"        // span: annotations
                          "\xF\0\x8\xC\0\0\0\0"        // span: binary_annotations
                          "\x2\0\x9\0"                 // span: debug
                          "\0"                         // end span
                          "\0"                         // end response header
                          "\x80\x1\0\x3\0\0\0\x7message\0\0\0\x1",
                          78),
              buffer.toString());
  }
}

// Tests isUpgradePrefix
TEST_F(TwitterProtocolTest, IsUpgradePrefix) {
  EXPECT_EQ(27, TwitterProtocolImpl::upgradeMethodName().length());

  // Doesn't start with magic bytes
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string(12, 'x'));
    EXPECT_FALSE(TwitterProtocolImpl::isUpgradePrefix(buffer));
  }

  // Message name length too short
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string("\x80\x01xx\0\0\0\x1Axxxx", 12));
    EXPECT_FALSE(TwitterProtocolImpl::isUpgradePrefix(buffer));
  }

  // Message name length too long
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string("\x80\x01xx\0\0\0\x1Cxxxx", 12));
    EXPECT_FALSE(TwitterProtocolImpl::isUpgradePrefix(buffer));
  }

  // Message name doesn't match expected
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string("\x80\x01xx\0\0\0\x1Bname", 12));
    EXPECT_FALSE(TwitterProtocolImpl::isUpgradePrefix(buffer));
  }

  // Message name doesn't match expected
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string("\x80\x01xx\0\0\0\x1B__caNOPE", 16));
    EXPECT_FALSE(TwitterProtocolImpl::isUpgradePrefix(buffer));
  }

  // Minimal match
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string("\x80\x01xx\0\0\0\x1B__ca", 12));
    EXPECT_TRUE(TwitterProtocolImpl::isUpgradePrefix(buffer));
  }

  // Complete match
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string("\x80\x01xx\0\0\0\x1B", 8) + TwitterProtocolImpl::upgradeMethodName());
    EXPECT_TRUE(TwitterProtocolImpl::isUpgradePrefix(buffer));
  }

  // Extra data
  {
    Buffer::OwnedImpl buffer;
    buffer.add(std::string("\x80\x01xx\0\0\0\x1B", 8) + TwitterProtocolImpl::upgradeMethodName() +
               "xxx");
    EXPECT_TRUE(TwitterProtocolImpl::isUpgradePrefix(buffer));
  }
}

} // Namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
