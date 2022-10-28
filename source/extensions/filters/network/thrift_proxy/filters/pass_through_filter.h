#pragma once

#include "source/extensions/filters/network/thrift_proxy/filters/filter.h"
#include "source/extensions/filters/network/thrift_proxy/passthrough_decoder_event_handler.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace ThriftFilters {

/**
 * Pass through Thrift decoder/encoder/bidirectional filter. Continue at each state within the
 * series of transitions, and pass through the decoded/encoded data.
 */
class PassThroughDecoderFilter : public DecoderFilter, public PassThroughDecoderEventHandler {
public:
  // Thrift FilterBase
  void onDestroy() override {}

  // Thrift DecoderFilter
  void setDecoderFilterCallbacks(DecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  };

  bool passthroughSupported() const override { return true; }

protected:
  DecoderFilterCallbacks* decoder_callbacks_{};
};

class PassThroughEncoderFilter : public EncoderFilter, public PassThroughDecoderEventHandler {
public:
  // Thrift FilterBase
  void onDestroy() override {}

  // Thrift EncoderFilter
  void setEncoderFilterCallbacks(EncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  };

  bool passthroughSupported() const override { return true; }

protected:
  EncoderFilterCallbacks* encoder_callbacks_{};
};

class PassThroughBidirectionalFilter : public BidirectionalFilter {
public:
  // ThriftFilterBase
  void onDestroy() override {}

  // Thrift DecoderFilter
  void setDecoderFilterCallbacks(DecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }

  // Thrift EncoderFilter
  void setEncoderFilterCallbacks(EncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }

  // Thrift Decoder/Encoder State Machine
  bool decodePassthroughSupported() const override { return true; }

  bool encodePassthroughSupported() const override { return true; }

  FilterStatus decodeTransportBegin(MessageMetadataSharedPtr) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus encodeTransportBegin(MessageMetadataSharedPtr) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus decodeTransportEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeTransportEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodePassthroughData(Buffer::Instance&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus encodePassthroughData(Buffer::Instance&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus decodeMessageBegin(MessageMetadataSharedPtr) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus encodeMessageBegin(MessageMetadataSharedPtr) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus decodeMessageEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeMessageEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodeStructBegin(absl::string_view) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus encodeStructBegin(absl::string_view) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus decodeStructEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeStructEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodeFieldBegin(absl::string_view, FieldType&, int16_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus encodeFieldBegin(absl::string_view, FieldType&, int16_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus decodeFieldEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeFieldEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodeBoolValue(bool&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeBoolValue(bool&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodeByteValue(uint8_t&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeByteValue(uint8_t&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodeInt16Value(int16_t&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeInt16Value(int16_t&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodeInt32Value(int32_t&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeInt32Value(int32_t&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodeInt64Value(int64_t&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeInt64Value(int64_t&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodeDoubleValue(double&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeDoubleValue(double&) override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodeStringValue(absl::string_view) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus encodeStringValue(absl::string_view) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus decodeMapBegin(FieldType&, FieldType&, uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus encodeMapBegin(FieldType&, FieldType&, uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus decodeMapEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeMapEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodeListBegin(FieldType&, uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus encodeListBegin(FieldType&, uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus decodeListEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeListEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus decodeSetBegin(FieldType&, uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus encodeSetBegin(FieldType&, uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  FilterStatus decodeSetEnd() override { return ThriftProxy::FilterStatus::Continue; }

  FilterStatus encodeSetEnd() override { return ThriftProxy::FilterStatus::Continue; }

protected:
  EncoderFilterCallbacks* encoder_callbacks_{};
  DecoderFilterCallbacks* decoder_callbacks_{};
};

} // namespace ThriftFilters
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
