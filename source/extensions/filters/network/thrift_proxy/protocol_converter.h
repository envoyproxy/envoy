#pragma once

#include "envoy/buffer/buffer.h"

#include "extensions/filters/network/thrift_proxy/filters/filter.h"
#include "extensions/filters/network/thrift_proxy/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * ProtocolConverter is an abstract class that implements protocol-related methods on
 * ThriftFilters::DecoderFilter in terms of converting the decoded messages into a different
 * protocol.
 */
class ProtocolConverter : public ThriftFilters::DecoderFilter {
public:
  ProtocolConverter() {}
  ~ProtocolConverter() {}

  void initProtocolConverter(ProtocolPtr&& proto, Buffer::Instance& buffer) {
    proto_ = std::move(proto);
    buffer_ = &buffer;
  }

  // ThiftFilters::DecoderFilter
  void onDestroy() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  void setDecoderFilterCallbacks(ThriftFilters::DecoderFilterCallbacks&) override {
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  void resetUpstreamConnection() override { NOT_IMPLEMENTED_GCOVR_EXCL_LINE; }
  ThriftFilters::FilterStatus messageBegin(absl::string_view name, MessageType msg_type,
                                           int32_t seq_id) override {
    proto_->writeMessageBegin(*buffer_, std::string(name), msg_type, seq_id);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus messageEnd() override {
    proto_->writeMessageEnd(*buffer_);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus structBegin(absl::string_view name) override {
    proto_->writeStructBegin(*buffer_, std::string(name));
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus structEnd() override {
    proto_->writeFieldBegin(*buffer_, "", FieldType::Stop, 0);
    proto_->writeStructEnd(*buffer_);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus fieldBegin(absl::string_view name, FieldType field_type,
                                         int16_t field_id) override {
    proto_->writeFieldBegin(*buffer_, std::string(name), field_type, field_id);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus fieldEnd() override {
    proto_->writeFieldEnd(*buffer_);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus boolValue(bool value) override {
    proto_->writeBool(*buffer_, value);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus byteValue(uint8_t value) override {
    proto_->writeByte(*buffer_, value);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus int16Value(int16_t value) override {
    proto_->writeInt16(*buffer_, value);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus int32Value(int32_t value) override {
    proto_->writeInt32(*buffer_, value);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus int64Value(int64_t value) override {
    proto_->writeInt64(*buffer_, value);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus doubleValue(double value) override {
    proto_->writeDouble(*buffer_, value);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus stringValue(absl::string_view value) override {
    proto_->writeString(*buffer_, std::string(value));
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus mapBegin(FieldType key_type, FieldType value_type,
                                       uint32_t size) override {
    proto_->writeMapBegin(*buffer_, key_type, value_type, size);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus mapEnd() override {
    proto_->writeMapEnd(*buffer_);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus listBegin(FieldType elem_type, uint32_t size) override {
    proto_->writeListBegin(*buffer_, elem_type, size);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus listEnd() override {
    proto_->writeListEnd(*buffer_);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus setBegin(FieldType elem_type, uint32_t size) override {
    proto_->writeSetBegin(*buffer_, elem_type, size);
    return ThriftFilters::FilterStatus::Continue;
  }

  ThriftFilters::FilterStatus setEnd() override {
    proto_->writeSetEnd(*buffer_);
    return ThriftFilters::FilterStatus::Continue;
  }

protected:
  ProtocolType protocolType() const { return proto_->type(); }

private:
  ProtocolPtr proto_;
  Buffer::Instance* buffer_{};
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
