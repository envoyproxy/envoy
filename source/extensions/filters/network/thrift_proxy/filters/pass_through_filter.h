#pragma once

#include "extensions/filters/network/thrift_proxy/filters/filter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace ThriftFilters {

/**
 * Pass through Thrift decoder filter. Continue at each decoding state within the series of
 * transitions.
 */
class PassThroughDecoderFilter : public DecoderFilter {
public:
  // ThriftDecoderFilter
  void onDestroy() override {}

  void setDecoderFilterCallbacks(DecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  };

  // Thrift Decoder State Machine
  ThriftProxy::FilterStatus transportBegin(ThriftProxy::MessageMetadataSharedPtr) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus transportEnd() override { return ThriftProxy::FilterStatus::Continue; }

  ThriftProxy::FilterStatus messageBegin(ThriftProxy::MessageMetadataSharedPtr) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus messageEnd() override { return ThriftProxy::FilterStatus::Continue; }

  ThriftProxy::FilterStatus structBegin(absl::string_view) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus structEnd() override { return ThriftProxy::FilterStatus::Continue; }

  ThriftProxy::FilterStatus fieldBegin(absl::string_view, ThriftProxy::FieldType&,
                                       int16_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus fieldEnd() override { return ThriftProxy::FilterStatus::Continue; }

  ThriftProxy::FilterStatus boolValue(bool&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus byteValue(uint8_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus int16Value(int16_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus int32Value(int32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus int64Value(int64_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus doubleValue(double&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus stringValue(absl::string_view) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus mapBegin(ThriftProxy::FieldType&, ThriftProxy::FieldType&,
                                     uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus mapEnd() override { return ThriftProxy::FilterStatus::Continue; }

  ThriftProxy::FilterStatus listBegin(ThriftProxy::FieldType&, uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus listEnd() override { return ThriftProxy::FilterStatus::Continue; }

  ThriftProxy::FilterStatus setBegin(ThriftProxy::FieldType&, uint32_t&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus setEnd() override { return ThriftProxy::FilterStatus::Continue; }

protected:
  DecoderFilterCallbacks* decoder_callbacks_{};
};

} // namespace ThriftFilters
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
