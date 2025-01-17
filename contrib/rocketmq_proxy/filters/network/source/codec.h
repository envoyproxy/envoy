#pragma once

#include <map>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/network/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/utility.h"

#include "contrib/rocketmq_proxy/filters/network/source/protocol.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

enum MessageVersion : uint32_t {
  V1 = (0xAABBCCDDU ^ 1880681586U) + 8U,
  V2 = (0xAABBCCDDU ^ 1880681586U) + 4U
};

class Decoder : Logger::Loggable<Logger::Id::rocketmq> {
public:
  Decoder() = default;

  ~Decoder() = default;

  /**
   * @param buffer Data buffer to decode.
   * @param underflow Indicate if buffer contains enough data in terms of protocol frame.
   * @param has_error Indicate if the decoding is successful or not.
   * @param request_code Corresponding request code if applies.
   * @return Decoded remote command.
   */
  static RemotingCommandPtr decode(Buffer::Instance& buffer, bool& underflow, bool& has_error,
                                   int request_code = 0);

  static std::string decodeTopic(Buffer::Instance& buffer, int32_t cursor);

  static int32_t decodeQueueId(Buffer::Instance& buffer, int32_t cursor);

  static int64_t decodeQueueOffset(Buffer::Instance& buffer, int32_t cursor);

  static std::string decodeMsgId(Buffer::Instance& buffer, int32_t cursor);

  static constexpr uint32_t MIN_FRAME_SIZE = 8;

  static constexpr uint32_t MAX_FRAME_SIZE = 4 * 1024 * 1024;

  static constexpr uint32_t FRAME_LENGTH_FIELD_SIZE = 4;

  static constexpr uint32_t FRAME_HEADER_LENGTH_FIELD_SIZE = 4;

private:
  static uint32_t adjustHeaderLength(uint32_t len) { return len & 0xFFFFFFu; }

  static bool isJsonHeader(uint32_t len) { return (len >> 24u) == 0; }

  static CommandCustomHeaderPtr decodeExtHeader(RequestCode code,
                                                ProtobufWkt::Struct& header_struct);

  static CommandCustomHeaderPtr decodeResponseExtHeader(ResponseCode response_code,
                                                        ProtobufWkt::Struct& header_struct,
                                                        RequestCode request_code);

  static bool isComplete(Buffer::Instance& buffer, int32_t cursor);
};

class Encoder {
public:
  static void encode(const RemotingCommandPtr& command, Buffer::Instance& buffer);
};

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
