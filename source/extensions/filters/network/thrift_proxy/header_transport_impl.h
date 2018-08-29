#pragma once

#include <string>

#include "envoy/buffer/buffer.h"

#include "extensions/filters/network/thrift_proxy/app_exception_impl.h"
#include "extensions/filters/network/thrift_proxy/metadata.h"
#include "extensions/filters/network/thrift_proxy/thrift.h"
#include "extensions/filters/network/thrift_proxy/transport.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * HeaderTransportImpl implements the Thrift Header transport.
 * See https://github.com/apache/thrift/blob/master/doc/specs/HeaderFormat.md and
 * https://github.com/apache/thrift/blob/master/lib/cpp/src/thrift/transport/THeaderTransport.h
 * (for constants not specified in the spec).
 */
class HeaderTransportImpl : public Transport {
public:
  // Transport
  const std::string& name() const override { return TransportNames::get().HEADER; }
  TransportType type() const override { return TransportType::Header; }
  bool decodeFrameStart(Buffer::Instance& buffer, MessageMetadata& metadata) override;
  bool decodeFrameEnd(Buffer::Instance& buffer) override;
  void encodeFrame(Buffer::Instance& buffer, const MessageMetadata& metadata,
                   Buffer::Instance& message) override;

  static bool isMagic(uint16_t word) { return word == Magic; }

  static constexpr int32_t MaxFrameSize = 0x3FFFFFFF;

private:
  static constexpr uint16_t Magic = 0x0FFF;

  static int16_t drainVarIntI16(Buffer::Instance& buffer, int32_t& header_size, const char* desc);
  static int32_t drainVarIntI32(Buffer::Instance& buffer, int32_t& header_size, const char* desc);
  static std::string drainVarString(Buffer::Instance& buffer, int32_t& header_size,
                                    const char* desc);
  static void writeVarString(Buffer::Instance& buffer, const absl::string_view str);

  void setException(AppExceptionType type, std::string reason) {
    if (exception_.has_value()) {
      return;
    }

    exception_ = type;
    exception_reason_ = reason;
  }

  absl::optional<AppExceptionType> exception_;
  std::string exception_reason_;
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
