#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "common/common/fmt.h"

#include "extensions/filters/network/thrift_proxy/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/*
 * ProtocolImplBase provides a base class for Protocol implementations.
 */
class ProtocolImplBase : public virtual Protocol {
public:
  ProtocolImplBase(ProtocolCallbacks& callbacks) : callbacks_(callbacks) {}

protected:
  void onMessageStart(const absl::string_view name, MessageType msg_type, int32_t seq_id) const {
    callbacks_.messageStart(name, msg_type, seq_id);
  }
  void onStructBegin(const absl::string_view name) const { callbacks_.structBegin(name); }
  void onStructField(const absl::string_view name, FieldType field_type, int16_t field_id) const {
    callbacks_.structField(name, field_type, field_id);
  }
  void onStructEnd() const { callbacks_.structEnd(); }
  void onMessageComplete() const { callbacks_.messageComplete(); }

  ProtocolCallbacks& callbacks_;
};

/**
 * AutoProtocolImpl attempts to distinguish between the Thrift binary (strict mode only) and
 * compact protocols and then delegates subsequent decoding operations to the appropriate Protocol
 * implementation.
 */
class AutoProtocolImpl : public ProtocolImplBase {
public:
  AutoProtocolImpl(ProtocolCallbacks& callbacks)
      : ProtocolImplBase(callbacks), name_(ProtocolNames::get().AUTO) {}

  // Protocol
  const std::string& name() const override { return name_; }
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
   * Explicitly set the protocol. Public to simplify testing.
   */
  void setProtocol(ProtocolPtr&& proto) {
    protocol_ = std::move(proto);
    name_ = fmt::format("{}({})", protocol_->name(), ProtocolNames::get().AUTO);
  }

private:
  ProtocolPtr protocol_{};
  std::string name_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
