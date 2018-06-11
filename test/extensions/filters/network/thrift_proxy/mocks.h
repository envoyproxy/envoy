#pragma once

#include "extensions/filters/network/thrift_proxy/protocol.h"
#include "extensions/filters/network/thrift_proxy/transport.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

class MockTransportCallbacks : public TransportCallbacks {
public:
  MockTransportCallbacks();
  ~MockTransportCallbacks();

  // ThriftProxy::TransportCallbacks
  MOCK_METHOD1(transportFrameStart, void(absl::optional<uint32_t> size));
  MOCK_METHOD0(transportFrameComplete, void());
};

class MockTransport : public Transport {
public:
  MockTransport();
  ~MockTransport();

  // ThriftProxy::Transport
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_METHOD1(decodeFrameStart, bool(Buffer::Instance&));
  MOCK_METHOD1(decodeFrameEnd, bool(Buffer::Instance&));

  std::string name_{"mock"};
};

class MockProtocolCallbacks : public ProtocolCallbacks {
public:
  MockProtocolCallbacks();
  ~MockProtocolCallbacks();

  // ThriftProxy::ProtocolCallbacks
  MOCK_METHOD3(messageStart, void(const absl::string_view, MessageType, int32_t));
  MOCK_METHOD1(structBegin, void(const absl::string_view));
  MOCK_METHOD3(structField, void(const absl::string_view, FieldType, int16_t));
  MOCK_METHOD0(structEnd, void());
  MOCK_METHOD0(messageComplete, void());
};

class MockProtocol : public Protocol {
public:
  MockProtocol();
  ~MockProtocol();

  // ThriftProxy::Protocol
  MOCK_CONST_METHOD0(name, const std::string&());
  MOCK_METHOD4(readMessageBegin, bool(Buffer::Instance& buffer, std::string& name,
                                      MessageType& msg_type, int32_t& seq_id));
  MOCK_METHOD1(readMessageEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD2(readStructBegin, bool(Buffer::Instance& buffer, std::string& name));
  MOCK_METHOD1(readStructEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD4(readFieldBegin, bool(Buffer::Instance& buffer, std::string& name,
                                    FieldType& field_type, int16_t& field_id));
  MOCK_METHOD1(readFieldEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD4(readMapBegin, bool(Buffer::Instance& buffer, FieldType& key_type,
                                  FieldType& value_type, uint32_t& size));
  MOCK_METHOD1(readMapEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD3(readListBegin, bool(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size));
  MOCK_METHOD1(readListEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD3(readSetBegin, bool(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size));
  MOCK_METHOD1(readSetEnd, bool(Buffer::Instance& buffer));
  MOCK_METHOD2(readBool, bool(Buffer::Instance& buffer, bool& value));
  MOCK_METHOD2(readByte, bool(Buffer::Instance& buffer, uint8_t& value));
  MOCK_METHOD2(readInt16, bool(Buffer::Instance& buffer, int16_t& value));
  MOCK_METHOD2(readInt32, bool(Buffer::Instance& buffer, int32_t& value));
  MOCK_METHOD2(readInt64, bool(Buffer::Instance& buffer, int64_t& value));
  MOCK_METHOD2(readDouble, bool(Buffer::Instance& buffer, double& value));
  MOCK_METHOD2(readString, bool(Buffer::Instance& buffer, std::string& value));
  MOCK_METHOD2(readBinary, bool(Buffer::Instance& buffer, std::string& value));

  std::string name_{"mock"};
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
