#include "source/extensions/filters/network/thrift_proxy/filter_utils.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace ThriftFilters {

class DelegateDecoderFilter final : public DecoderFilter {
public:
  DelegateDecoderFilter(BidirectionalFilterSharedPtr filter) : parent_(filter){};
  // ThriftBaseFilter
  void onDestroy() override { throw EnvoyException(fmt::format("should not be called")); }

  void setDecoderFilterCallbacks(DecoderFilterCallbacks& callbacks) override {
    return parent_->setDecoderFilterCallbacks(callbacks);
  };

  // Thrift Decoder State Machine
  ThriftProxy::FilterStatus
  transportBegin(ThriftProxy::MessageMetadataSharedPtr metadata) override {
    return parent_->decodeTransportBegin(metadata);
  }

  ThriftProxy::FilterStatus transportEnd() override { return parent_->decodeTransportEnd(); }

  bool passthroughSupported() const override { return parent_->decodePassthroughSupported(); }

  ThriftProxy::FilterStatus passthroughData(Buffer::Instance& data) override {
    return parent_->decodePassthroughData(data);
  }

  ThriftProxy::FilterStatus messageBegin(ThriftProxy::MessageMetadataSharedPtr metadata) override {
    return parent_->decodeMessageBegin(metadata);
  }

  ThriftProxy::FilterStatus messageEnd() override { return parent_->decodeMessageEnd(); }

  ThriftProxy::FilterStatus structBegin(absl::string_view name) override {
    return parent_->decodeStructBegin(name);
  }

  ThriftProxy::FilterStatus structEnd() override { return parent_->decodeStructEnd(); }

  ThriftProxy::FilterStatus fieldBegin(absl::string_view name, ThriftProxy::FieldType& field_type,
                                       int16_t& field_id) override {
    return parent_->decodeFieldBegin(name, field_type, field_id);
  }

  ThriftProxy::FilterStatus fieldEnd() override { return parent_->decodeFieldEnd(); }

  ThriftProxy::FilterStatus boolValue(bool& value) override {
    return parent_->decodeBoolValue(value);
  }

  ThriftProxy::FilterStatus byteValue(uint8_t& value) override {
    return parent_->decodeByteValue(value);
  }

  ThriftProxy::FilterStatus int16Value(int16_t& value) override {
    return parent_->decodeInt16Value(value);
  }

  ThriftProxy::FilterStatus int32Value(int32_t& value) override {
    return parent_->decodeInt32Value(value);
  }

  ThriftProxy::FilterStatus int64Value(int64_t& value) override {
    return parent_->decodeInt64Value(value);
  }

  ThriftProxy::FilterStatus doubleValue(double& value) override {
    return parent_->decodeDoubleValue(value);
  }

  ThriftProxy::FilterStatus stringValue(absl::string_view value) override {
    return parent_->decodeStringValue(value);
  }

  ThriftProxy::FilterStatus mapBegin(ThriftProxy::FieldType& key_type,
                                     ThriftProxy::FieldType& value_type, uint32_t& size) override {
    return parent_->decodeMapBegin(key_type, value_type, size);
  }

  ThriftProxy::FilterStatus mapEnd() override { return parent_->decodeMapEnd(); }

  ThriftProxy::FilterStatus listBegin(ThriftProxy::FieldType& elem_type, uint32_t& size) override {
    return parent_->decodeListBegin(elem_type, size);
  }

  ThriftProxy::FilterStatus listEnd() override { return parent_->decodeListEnd(); }

  ThriftProxy::FilterStatus setBegin(ThriftProxy::FieldType& elem_type, uint32_t& size) override {
    return parent_->decodeSetBegin(elem_type, size);
  }

  ThriftProxy::FilterStatus setEnd() override { return parent_->decodeSetEnd(); }

private:
  BidirectionalFilterSharedPtr parent_;
};

using DelegateDecoderFilterSharedPtr = std::shared_ptr<DelegateDecoderFilter>;

class DelegateEncoderFilter final : public EncoderFilter {
public:
  DelegateEncoderFilter(BidirectionalFilterSharedPtr filter) : parent_(filter){};
  // ThriftBaseFilter
  void onDestroy() override { throw EnvoyException(fmt::format("should not be called")); }

  void setEncoderFilterCallbacks(EncoderFilterCallbacks& callbacks) override {
    return parent_->setEncoderFilterCallbacks(callbacks);
  };

  // Thrift Encoder State Machine
  ThriftProxy::FilterStatus
  transportBegin(ThriftProxy::MessageMetadataSharedPtr metadata) override {
    return parent_->encodeTransportBegin(metadata);
  }

  ThriftProxy::FilterStatus transportEnd() override { return parent_->encodeTransportEnd(); }

  bool passthroughSupported() const override { return parent_->encodePassthroughSupported(); }

  ThriftProxy::FilterStatus passthroughData(Buffer::Instance& data) override {
    return parent_->encodePassthroughData(data);
  }

  ThriftProxy::FilterStatus messageBegin(ThriftProxy::MessageMetadataSharedPtr metadata) override {
    return parent_->encodeMessageBegin(metadata);
  }

  ThriftProxy::FilterStatus messageEnd() override { return parent_->encodeMessageEnd(); }

  ThriftProxy::FilterStatus structBegin(absl::string_view name) override {
    return parent_->encodeStructBegin(name);
  }

  ThriftProxy::FilterStatus structEnd() override { return parent_->encodeStructEnd(); }

  ThriftProxy::FilterStatus fieldBegin(absl::string_view name, ThriftProxy::FieldType& field_type,
                                       int16_t& field_id) override {
    return parent_->encodeFieldBegin(name, field_type, field_id);
  }

  ThriftProxy::FilterStatus fieldEnd() override { return parent_->encodeFieldEnd(); }

  ThriftProxy::FilterStatus boolValue(bool& value) override {
    return parent_->encodeBoolValue(value);
  }

  ThriftProxy::FilterStatus byteValue(uint8_t& value) override {
    return parent_->encodeByteValue(value);
  }

  ThriftProxy::FilterStatus int16Value(int16_t& value) override {
    return parent_->encodeInt16Value(value);
  }

  ThriftProxy::FilterStatus int32Value(int32_t& value) override {
    return parent_->encodeInt32Value(value);
  }

  ThriftProxy::FilterStatus int64Value(int64_t& value) override {
    return parent_->encodeInt64Value(value);
  }

  ThriftProxy::FilterStatus doubleValue(double& value) override {
    return parent_->encodeDoubleValue(value);
  }

  ThriftProxy::FilterStatus stringValue(absl::string_view value) override {
    return parent_->encodeStringValue(value);
  }

  ThriftProxy::FilterStatus mapBegin(ThriftProxy::FieldType& key_type,
                                     ThriftProxy::FieldType& value_type, uint32_t& size) override {
    return parent_->encodeMapBegin(key_type, value_type, size);
  }

  ThriftProxy::FilterStatus mapEnd() override { return parent_->encodeMapEnd(); }

  ThriftProxy::FilterStatus listBegin(ThriftProxy::FieldType& elem_type, uint32_t& size) override {
    return parent_->encodeListBegin(elem_type, size);
  }

  ThriftProxy::FilterStatus listEnd() override { return parent_->encodeListEnd(); }

  ThriftProxy::FilterStatus setBegin(ThriftProxy::FieldType& elem_type, uint32_t& size) override {
    return parent_->encodeSetBegin(elem_type, size);
  }

  ThriftProxy::FilterStatus setEnd() override { return parent_->encodeSetEnd(); }

private:
  BidirectionalFilterSharedPtr parent_;
};

using DelegateEncoderFilterSharedPtr = std::shared_ptr<DelegateEncoderFilter>;

BidirectionalFilterWrapper::BidirectionalFilterWrapper(BidirectionalFilterSharedPtr filter)
    : decoder_filter_(std::make_shared<DelegateDecoderFilter>(filter)),
      encoder_filter_(std::make_shared<DelegateEncoderFilter>(filter)), parent_(filter) {}

} // namespace ThriftFilters
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
