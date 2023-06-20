#pragma once

#include "source/extensions/filters/network/thrift_proxy/decoder_events.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
/**
 * Pass through Thrift decoder event handler. Continue at each state within the
 * series of transitions.
 */
class PassThroughDecoderEventHandler : public virtual DecoderEventHandler {
public:
  ThriftProxy::FilterStatus transportBegin(ThriftProxy::MessageMetadataSharedPtr) override {
    return ThriftProxy::FilterStatus::Continue;
  }

  ThriftProxy::FilterStatus transportEnd() override { return ThriftProxy::FilterStatus::Continue; }

  ThriftProxy::FilterStatus passthroughData(Buffer::Instance&) override {
    return ThriftProxy::FilterStatus::Continue;
  }

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
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
