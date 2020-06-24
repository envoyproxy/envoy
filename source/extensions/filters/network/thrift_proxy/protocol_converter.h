#pragma once

#include "envoy/buffer/buffer.h"

#include "extensions/filters/network/thrift_proxy/decoder_events.h"
#include "extensions/filters/network/thrift_proxy/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * ProtocolConverter is an abstract class that implements protocol-related methods on
 * DecoderEventHandler in terms of converting the decoded messages into a different protocol.
 */
class ProtocolConverter : public virtual DecoderEventHandler {
public:
  ProtocolConverter() = default;
  ~ProtocolConverter() override = default;

  void initProtocolConverter(Protocol& proto, Buffer::Instance& buffer) {
    proto_ = &proto;
    buffer_ = &buffer;
  }

  // DecoderEventHandler
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override {
    proto_->writeMessageBegin(*buffer_, *metadata);
    return FilterStatus::Continue;
  }

  FilterStatus messageEnd() override {
    proto_->writeMessageEnd(*buffer_);
    return FilterStatus::Continue;
  }

  FilterStatus structBegin(absl::string_view name) override {
    proto_->writeStructBegin(*buffer_, std::string(name));
    return FilterStatus::Continue;
  }

  FilterStatus structEnd() override {
    proto_->writeFieldBegin(*buffer_, "", FieldType::Stop, 0);
    proto_->writeStructEnd(*buffer_);
    return FilterStatus::Continue;
  }

  FilterStatus fieldBegin(absl::string_view name, FieldType& field_type,
                          int16_t& field_id) override {
    proto_->writeFieldBegin(*buffer_, std::string(name), field_type, field_id);
    return FilterStatus::Continue;
  }

  FilterStatus fieldEnd() override {
    proto_->writeFieldEnd(*buffer_);
    return FilterStatus::Continue;
  }

  FilterStatus boolValue(bool& value) override {
    proto_->writeBool(*buffer_, value);
    return FilterStatus::Continue;
  }

  FilterStatus byteValue(uint8_t& value) override {
    proto_->writeByte(*buffer_, value);
    return FilterStatus::Continue;
  }

  FilterStatus int16Value(int16_t& value) override {
    proto_->writeInt16(*buffer_, value);
    return FilterStatus::Continue;
  }

  FilterStatus int32Value(int32_t& value) override {
    proto_->writeInt32(*buffer_, value);
    return FilterStatus::Continue;
  }

  FilterStatus int64Value(int64_t& value) override {
    proto_->writeInt64(*buffer_, value);
    return FilterStatus::Continue;
  }

  FilterStatus doubleValue(double& value) override {
    proto_->writeDouble(*buffer_, value);
    return FilterStatus::Continue;
  }

  FilterStatus stringValue(absl::string_view value) override {
    proto_->writeString(*buffer_, std::string(value));
    return FilterStatus::Continue;
  }

  FilterStatus mapBegin(FieldType& key_type, FieldType& value_type, uint32_t& size) override {
    proto_->writeMapBegin(*buffer_, key_type, value_type, size);
    return FilterStatus::Continue;
  }

  FilterStatus mapEnd() override {
    proto_->writeMapEnd(*buffer_);
    return FilterStatus::Continue;
  }

  FilterStatus listBegin(FieldType& elem_type, uint32_t& size) override {
    proto_->writeListBegin(*buffer_, elem_type, size);
    return FilterStatus::Continue;
  }

  FilterStatus listEnd() override {
    proto_->writeListEnd(*buffer_);
    return FilterStatus::Continue;
  }

  FilterStatus setBegin(FieldType& elem_type, uint32_t& size) override {
    proto_->writeSetBegin(*buffer_, elem_type, size);
    return FilterStatus::Continue;
  }

  FilterStatus setEnd() override {
    proto_->writeSetEnd(*buffer_);
    return FilterStatus::Continue;
  }

private:
  Protocol* proto_;
  Buffer::Instance* buffer_{};
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
