#pragma once
#include <cstdint>

#include "envoy/common/platform.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace PostgresProxy {

// Postgres response encoder.
class Encoder : Logger::Loggable<Logger::Id::filter> {
public:
  Encoder() = default;

  /**
   * Build a PostgreSQL error response buffer.
   * Format response according to
   * https://www.postgresql.org/docs/current/protocol-message-formats.html#PROTOCOL-MESSAGE-FORMATS-ERRORRESPONSE
   * Include severity and code according to
   * https://www.postgresql.org/docs/current/protocol-error-fields.html
   *
   * @param severity (e.g., "ERROR", "FATAL")
   * @param message error message
   * @param code the SQLSTATE code for the error(e.g., "28000"). Official doc:
   * https://www.postgresql.org/docs/current/errcodes-appendix.html
   * @return Buffer containing the response message
   */
  Envoy::Buffer::OwnedImpl buildErrorResponse(absl::string_view severity, absl::string_view message,
                                              absl::string_view code);
};

using EncoderPtr = std::unique_ptr<Encoder>;

} // namespace PostgresProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
