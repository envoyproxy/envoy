#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "source/common/common/fmt.h"
#include "source/extensions/filters/network/thrift_proxy/protocol.h"
#include "source/extensions/filters/network/thrift_proxy/thrift.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * AutoProtocolImpl attempts to distinguish between the Thrift binary (strict mode only), compact,
 * and Twitter protocols and then delegates subsequent decoding operations to the appropriate
 * Protocol implementation.
 */
class AutoProtocolImpl : public Protocol {
public:
  AutoProtocolImpl() : name_(ProtocolNames::get().AUTO) {}

  // Protocol
  const std::string& name() const override { return name_; }
  ProtocolType type() const override {
    if (protocol_ != nullptr) {
      return protocol_->type();
    }
    return ProtocolType::Auto;
  }
  void setType(ProtocolType type) override;

  bool readMessageBegin(Buffer::Instance& buffer, MessageMetadata& metadata) override;
  bool readMessageEnd(Buffer::Instance& buffer) override;
  bool peekReplyPayload(Buffer::Instance& buffer, ReplyType& reply_type) override {
    return protocol_->peekReplyPayload(buffer, reply_type);
  }
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
  void writeMessageBegin(Buffer::Instance& buffer, const MessageMetadata& metadata) override {
    protocol_->writeMessageBegin(buffer, metadata);
  }
  void writeMessageEnd(Buffer::Instance& buffer) override { protocol_->writeMessageEnd(buffer); }
  void writeStructBegin(Buffer::Instance& buffer, const std::string& name) override {
    protocol_->writeStructBegin(buffer, name);
  }
  void writeStructEnd(Buffer::Instance& buffer) override { protocol_->writeStructEnd(buffer); }
  void writeFieldBegin(Buffer::Instance& buffer, const std::string& name, FieldType field_type,
                       int16_t field_id) override {
    protocol_->writeFieldBegin(buffer, name, field_type, field_id);
  }
  void writeFieldEnd(Buffer::Instance& buffer) override { protocol_->writeFieldEnd(buffer); }
  void writeMapBegin(Buffer::Instance& buffer, FieldType key_type, FieldType value_type,
                     uint32_t size) override {
    protocol_->writeMapBegin(buffer, key_type, value_type, size);
  }
  void writeMapEnd(Buffer::Instance& buffer) override { protocol_->writeMapEnd(buffer); }
  void writeListBegin(Buffer::Instance& buffer, FieldType elem_type, uint32_t size) override {
    protocol_->writeListBegin(buffer, elem_type, size);
  }
  void writeListEnd(Buffer::Instance& buffer) override { protocol_->writeListEnd(buffer); }
  void writeSetBegin(Buffer::Instance& buffer, FieldType elem_type, uint32_t size) override {
    protocol_->writeSetBegin(buffer, elem_type, size);
  }
  void writeSetEnd(Buffer::Instance& buffer) override { protocol_->writeSetEnd(buffer); }
  void writeBool(Buffer::Instance& buffer, bool value) override {
    protocol_->writeBool(buffer, value);
  }
  void writeByte(Buffer::Instance& buffer, uint8_t value) override {
    protocol_->writeByte(buffer, value);
  }
  void writeInt16(Buffer::Instance& buffer, int16_t value) override {
    protocol_->writeInt16(buffer, value);
  }
  void writeInt32(Buffer::Instance& buffer, int32_t value) override {
    protocol_->writeInt32(buffer, value);
  }
  void writeInt64(Buffer::Instance& buffer, int64_t value) override {
    protocol_->writeInt64(buffer, value);
  }
  void writeDouble(Buffer::Instance& buffer, double value) override {
    protocol_->writeDouble(buffer, value);
  }
  void writeString(Buffer::Instance& buffer, const std::string& value) override {
    protocol_->writeString(buffer, value);
  }
  void writeBinary(Buffer::Instance& buffer, const std::string& value) override {
    protocol_->writeBinary(buffer, value);
  }
  bool supportsUpgrade() override { return protocol_->supportsUpgrade(); }
  DecoderEventHandlerSharedPtr upgradeRequestDecoder() override {
    return protocol_->upgradeRequestDecoder();
  }
  DirectResponsePtr upgradeResponse(const DecoderEventHandler& decoder) override {
    return protocol_->upgradeResponse(decoder);
  }
  ThriftObjectPtr attemptUpgrade(Transport& transport, ThriftConnectionState& state,
                                 Buffer::Instance& buffer) override {
    return protocol_->attemptUpgrade(transport, state, buffer);
  }
  void completeUpgrade(ThriftConnectionState& state, ThriftObject& response) override {
    return protocol_->completeUpgrade(state, response);
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
