#pragma once

#include "source/extensions/common/dubbo/serializer.h"

#include "message.h"
#include "metadata.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Dubbo {

enum DecodeStatus {
  Success,
  Failure,
  Waiting,
};

class DubboCodec;
using DubboCodecPtr = std::unique_ptr<DubboCodec>;

class DubboCodec {
public:
  static DubboCodecPtr codecFromSerializeType(SerializeType type);

  DecodeStatus decodeHeader(Buffer::Instance& buffer, MessageMetadata& metadata);

  DecodeStatus decodeData(Buffer::Instance& buffer, MessageMetadata& metadata);

  void encode(Buffer::Instance& buffer, MessageMetadata& metadata);

  void initilize(SerializerPtr serializer) { serializer_ = std::move(serializer); }

  const SerializerPtr& serializer() const { return serializer_; }

  // Encode header only. In most cases, the 'encode' should be used and this method
  // just used for test.
  void encodeHeaderForTest(Buffer::Instance& buffer, Context& context);

  static constexpr uint8_t HeadersSize = 16;
  static constexpr int32_t MaxBodySize = 16 * 1024 * 1024;

private:
  SerializerPtr serializer_{};
};

class DirectResponseUtil {
public:
  static MessageMetadataSharedPtr heartbeatResponse(MessageMetadata& heartbeat_request);
  static MessageMetadataSharedPtr localResponse(MessageMetadata& request, ResponseStatus status,
                                                absl::optional<RpcResponseType> type,
                                                absl::string_view content);
};

} // namespace Dubbo
} // namespace Common
} // namespace Extensions
} // namespace Envoy
