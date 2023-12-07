#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/platform.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/byte_order.h"
#include "source/common/common/logger.h"

#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ZooKeeperProxy {

/**
 * Helper for extracting ZooKeeper data from a buffer.
 *
 * If at any point a peek is tried beyond max_len, an invalid argument error
 * will be returned. This is important to protect Envoy against malformed
 * requests (e.g.: when the declared and actual length don't match).
 *
 * Note: ZooKeeper's protocol uses network byte ordering (big-endian).
 */
class BufferHelper : public Logger::Loggable<Logger::Id::filter> {
public:
  BufferHelper(uint32_t max_len) : max_len_(max_len) {}

  absl::StatusOr<int32_t> peekInt32(Buffer::Instance& buffer, uint64_t& offset);
  absl::StatusOr<int64_t> peekInt64(Buffer::Instance& buffer, uint64_t& offset);
  absl::StatusOr<std::string> peekString(Buffer::Instance& buffer, uint64_t& offset);
  absl::StatusOr<bool> peekBool(Buffer::Instance& buffer, uint64_t& offset);
  void skip(uint32_t len, uint64_t& offset);
  void reset() { current_ = 0; }

private:
  absl::Status ensureMaxLen(uint32_t size);

  const uint32_t max_len_;
  uint32_t current_{};
};

#define ABSL_STATUS_RETURN_IF_STATUS_NOT_OK(status)                                                \
  if (!status.ok()) {                                                                              \
    return status;                                                                                 \
  }

#define EMIT_DECODER_ERR_AND_RETURN_IF_STATUS_NOT_OK(status, opcode)                               \
  if (!status.ok()) {                                                                              \
    callbacks_.onDecodeError(opcode);                                                              \
    return status;                                                                                 \
  }

#define RETURN_INVALID_ARG_ERR_IF_STATUS_NOT_OK(status, message)                                   \
  if (!status.ok()) {                                                                              \
    return absl::InvalidArgumentError(message);                                                    \
  }

#define EMIT_DECODER_ERR_AND_RETURN_INVALID_ARG_ERR_IF_STATUS_NOT_OK(status, opcode, message)      \
  if (!status.ok()) {                                                                              \
    callbacks_.onDecodeError(opcode);                                                              \
    return absl::InvalidArgumentError(message);                                                    \
  }
} // namespace ZooKeeperProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
