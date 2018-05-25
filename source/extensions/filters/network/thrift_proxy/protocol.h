#pragma once

#include <memory>
#include <stack>
#include <string>
#include <utility>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/pure.h"

#include "common/common/assert.h"
#include "common/common/macros.h"
#include "common/singleton/const_singleton.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * Names of available Protocol implementations.
 */
class ProtocolNameValues {
public:
  // Binary protocol
  const std::string BINARY = "binary";

  // Lax Binary protocol
  const std::string LAX_BINARY = "binary/non-strict";

  // Compact protocol
  const std::string COMPACT = "compact";

  // JSON protocol
  const std::string JSON = "json";

  // Auto-detection protocol
  const std::string AUTO = "auto";
};

typedef ConstSingleton<ProtocolNameValues> ProtocolNames;

/**
 * Thrift protocol message types.
 * See https://github.com/apache/thrift/blob/master/lib/cpp/src/thrift/protocol/TProtocol.h
 */
enum MessageType {
  Call = 1,
  Reply = 2,
  Exception = 3,
  Oneway = 4,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST MESSAGE TYPE
  LastMessageType = Oneway,
};

/**
 * Thrift protocol struct field types.
 * See https://github.com/apache/thrift/blob/master/lib/cpp/src/thrift/protocol/TProtocol.h
 */
enum FieldType {
  Stop = 0,
  Void = 1,
  Bool = 2,
  Byte = 3,
  Double = 4,
  I16 = 6,
  I32 = 8,
  I64 = 10,
  String = 11,
  Struct = 12,
  Map = 13,
  Set = 14,
  List = 15,

  // ATTENTION: MAKE SURE THIS REMAINS EQUAL TO THE LAST FIELD TYPE
  LastFieldType = List,
};

/**
 * Protocol represents the operations necessary to implement the a generic Thrift protocol.
 * See https://github.com/apache/thrift/blob/master/doc/specs/thrift-protocol-spec.md
 */
class Protocol {
public:
  virtual ~Protocol() {}

  virtual std::string name() const PURE;

  virtual bool readMessageBegin(Buffer::Instance& buffer, std::string& name, MessageType& msg_type,
                                int32_t& seq_id) PURE;
  virtual bool readMessageEnd(Buffer::Instance& buffer) PURE;
  virtual bool readStructBegin(Buffer::Instance& buffer, std::string& name) PURE;
  virtual bool readStructEnd(Buffer::Instance& buffer) PURE;
  virtual bool readFieldBegin(Buffer::Instance& buffer, std::string& name, FieldType& field_type,
                              int16_t& field_id) PURE;
  virtual bool readFieldEnd(Buffer::Instance& buffer) PURE;
  virtual bool readMapBegin(Buffer::Instance& buffer, FieldType& key_type, FieldType& value_type,
                            uint32_t& size) PURE;
  virtual bool readMapEnd(Buffer::Instance& buffer) PURE;
  virtual bool readListBegin(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size) PURE;
  virtual bool readListEnd(Buffer::Instance& buffer) PURE;
  virtual bool readSetBegin(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size) PURE;
  virtual bool readSetEnd(Buffer::Instance& buffer) PURE;
  virtual bool readBool(Buffer::Instance& buffer, bool& value) PURE;
  virtual bool readByte(Buffer::Instance& buffer, uint8_t& value) PURE;
  virtual bool readInt16(Buffer::Instance& buffer, int16_t& value) PURE;
  virtual bool readInt32(Buffer::Instance& buffer, int32_t& value) PURE;
  virtual bool readInt64(Buffer::Instance& buffer, int64_t& value) PURE;
  virtual bool readDouble(Buffer::Instance& buffer, double& value) PURE;
  virtual bool readString(Buffer::Instance& buffer, std::string& value) PURE;
  virtual bool readBinary(Buffer::Instance& buffer, std::string& value) PURE;
};

typedef std::unique_ptr<Protocol> ProtocolPtr;

/**
 * AutoProtocolImpl attempts to distinguish between the Thrift binary (strict mode only) and
 * compact protocols and then delegates subsequent decoding operations to the appropriate Protocol
 * implementation.
 */
class AutoProtocolImpl : public virtual Protocol {
public:
  // Protocol
  std::string name() const override;
  bool readMessageBegin(Buffer::Instance& buffer, std::string& name, MessageType& msg_type,
                        int32_t& seq_id) override;
  bool readMessageEnd(Buffer::Instance& buffer) override;
  bool readStructBegin(Buffer::Instance& buffer, std::string& name) override {
    return protocol_->readStructBegin(buffer, name);
  }
  bool readStructEnd(Buffer::Instance& buffer) override { return protocol_->readStructEnd(buffer); }
  bool readFieldBegin(Buffer::Instance& buffer, std::string& name, FieldType& field_type,
                      int16_t& field_id) override {
    return protocol_->readFieldBegin(buffer, name, field_type, field_id);
  }
  bool readFieldEnd(Buffer::Instance& buffer) override { return protocol_->readFieldEnd(buffer); }
  bool readMapBegin(Buffer::Instance& buffer, FieldType& key_type, FieldType& value_type,
                    uint32_t& size) override {
    return protocol_->readMapBegin(buffer, key_type, value_type, size);
  }
  bool readMapEnd(Buffer::Instance& buffer) override { return protocol_->readMapEnd(buffer); }
  bool readListBegin(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size) override {
    return protocol_->readListBegin(buffer, elem_type, size);
  }
  bool readListEnd(Buffer::Instance& buffer) override { return protocol_->readListEnd(buffer); }
  bool readSetBegin(Buffer::Instance& buffer, FieldType& elem_type, uint32_t& size) override {
    return protocol_->readSetBegin(buffer, elem_type, size);
  }
  bool readSetEnd(Buffer::Instance& buffer) override { return protocol_->readSetEnd(buffer); }
  bool readBool(Buffer::Instance& buffer, bool& value) override {
    return protocol_->readBool(buffer, value);
  }
  bool readByte(Buffer::Instance& buffer, uint8_t& value) override {
    return protocol_->readByte(buffer, value);
  }
  bool readInt16(Buffer::Instance& buffer, int16_t& value) override {
    return protocol_->readInt16(buffer, value);
  }
  bool readInt32(Buffer::Instance& buffer, int32_t& value) override {
    return protocol_->readInt32(buffer, value);
  }
  bool readInt64(Buffer::Instance& buffer, int64_t& value) override {
    return protocol_->readInt64(buffer, value);
  }
  bool readDouble(Buffer::Instance& buffer, double& value) override {
    return protocol_->readDouble(buffer, value);
  }
  bool readString(Buffer::Instance& buffer, std::string& value) override {
    return protocol_->readString(buffer, value);
  }
  bool readBinary(Buffer::Instance& buffer, std::string& value) override {
    return protocol_->readBinary(buffer, value);
  }

  /*
   * Explicitly set the protocol. Allows for testing.
   */
  void setProtocol(ProtocolPtr&& proto) { protocol_ = std::move(proto); }

private:
  ProtocolPtr protocol_{};
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
