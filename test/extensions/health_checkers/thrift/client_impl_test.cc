#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/utility.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/health_checkers/thrift/client_impl.h"

#include "test/extensions/health_checkers/thrift/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/upstream/host.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Eq;
using testing::InSequence;
using testing::Invoke;
using testing::Property;
using testing::Ref;
using testing::Return;
using testing::SaveArg;

namespace Envoy {
namespace Extensions {
namespace HealthCheckers {
namespace ThriftHealthChecker {

using namespace NetworkFilters::ThriftProxy;

class ThriftClientImplTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  ThriftClientImplTest() = default;
  ~ThriftClientImplTest() override = default;

  void setup() {
    connection_ = new NiceMock<Network::MockClientConnection>();
    conn_info_.connection_ = connection_;

    EXPECT_CALL(callback_, createConnection_).WillOnce(Return(conn_info_));
    EXPECT_CALL(*connection_, addReadFilter(_)).WillOnce(SaveArg<0>(&upstream_read_filter_));
    EXPECT_CALL(*connection_, connect());
    EXPECT_CALL(*connection_, noDelay(true));

    client_ = std::make_unique<ClientImpl>(callback_, transport_, protocol_, method_name_, host_,
                                           initial_seq_id_);
    client_->start();

    // No-OP currently.
    connection_->runHighWatermarkCallbacks();
    connection_->runLowWatermarkCallbacks();
  }

  void onConnected() {}

  void writeMessage(Buffer::Instance& buffer, NetworkFilters::ThriftProxy::MessageType msg_type) {
    Buffer::OwnedImpl msg;
    ProtocolPtr proto = NamedProtocolConfigFactory::getFactory(protocol_).createProtocol();
    MessageMetadata metadata;
    metadata.setProtocol(protocol_);
    metadata.setMethodName(method_name_);
    metadata.setMessageType(msg_type);
    metadata.setSequenceId(initial_seq_id_);

    proto->writeMessageBegin(msg, metadata);
    proto->writeStructBegin(msg, "response");
    proto->writeFieldBegin(msg, "success", FieldType::String, 0);
    proto->writeString(msg, "field");
    proto->writeFieldEnd(msg);
    proto->writeFieldBegin(msg, "", FieldType::Stop, 0);
    proto->writeStructEnd(msg);
    proto->writeMessageEnd(msg);

    TransportPtr transport = NamedTransportConfigFactory::getFactory(transport_).createTransport();
    transport->encodeFrame(buffer, metadata, msg);
  }

  void writeFramedBinaryIDLException(Buffer::Instance& buffer) {
    Buffer::OwnedImpl msg;
    ProtocolPtr proto = NamedProtocolConfigFactory::getFactory(protocol_).createProtocol();
    MessageMetadata metadata;
    metadata.setProtocol(protocol_);
    metadata.setMethodName(method_name_);
    metadata.setMessageType(MessageType::Reply);
    metadata.setSequenceId(initial_seq_id_);

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

    TransportPtr transport = NamedTransportConfigFactory::getFactory(transport_).createTransport();
    transport->encodeFrame(buffer, metadata, msg);
  }

  std::shared_ptr<Upstream::MockHost> host_{new NiceMock<Upstream::MockHost>()};
  NiceMock<Network::MockClientConnection>* connection_{};
  Network::ReadFilterSharedPtr upstream_read_filter_;
  ClientPtr client_;
  Upstream::MockHost::MockCreateConnectionData conn_info_;
  NiceMock<MockClientCallback> callback_;
  const NetworkFilters::ThriftProxy::TransportType transport_{
      NetworkFilters::ThriftProxy::TransportType::Header};
  const NetworkFilters::ThriftProxy::ProtocolType protocol_{
      NetworkFilters::ThriftProxy::ProtocolType::Binary};
  const std::string method_name_{"foo"};
  const int32_t initial_seq_id_{9527};
};

TEST_F(ThriftClientImplTest, Success) {
  InSequence s;

  setup();

  // Expect client write the simple request.
  EXPECT_CALL(*connection_, write(_, _));

  bool success = client_->makeRequest();
  EXPECT_TRUE(success);

  EXPECT_CALL(callback_, onResponseResult(true));

  Buffer::OwnedImpl success_response;
  writeMessage(success_response, NetworkFilters::ThriftProxy::MessageType::Reply);
  upstream_read_filter_->onData(success_response, false);

  client_->close();
}

TEST_F(ThriftClientImplTest, Execption) {
  InSequence s;

  setup();

  // Expect client write the simple request.
  EXPECT_CALL(*connection_, write(_, _));

  bool success = client_->makeRequest();
  EXPECT_TRUE(success);

  // Exception fails the response.
  EXPECT_CALL(callback_, onResponseResult(false));

  Buffer::OwnedImpl exception_response;
  writeMessage(exception_response, NetworkFilters::ThriftProxy::MessageType::Exception);
  upstream_read_filter_->onData(exception_response, false);

  EXPECT_CALL(*connection_, close(_));
  client_->close();
}

TEST_F(ThriftClientImplTest, Error) {
  InSequence s;

  setup();

  // Expect client write the simple request.
  EXPECT_CALL(*connection_, write(_, _));

  bool success = client_->makeRequest();
  EXPECT_TRUE(success);

  // Exception fails the response.
  EXPECT_CALL(callback_, onResponseResult(false));

  Buffer::OwnedImpl idl_exception_response;
  writeFramedBinaryIDLException(idl_exception_response);
  upstream_read_filter_->onData(idl_exception_response, false);

  EXPECT_CALL(*connection_, close(_));
  client_->close();
}

} // namespace ThriftHealthChecker
} // namespace HealthCheckers
} // namespace Extensions
} // namespace Envoy
