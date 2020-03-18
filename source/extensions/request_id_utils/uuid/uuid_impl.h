#pragma once

#include "envoy/request_id_utils/request_id_utils.h"

#include "common/runtime/runtime_impl.h"

namespace Envoy {
namespace Extensions {
namespace RequestIDUtils {
class UUIDUtils : public Envoy::RequestIDUtils::Utilities {
public:
  UUIDUtils(Envoy::Runtime::RandomGenerator& random) : random(random) {}

  void setRequestID(Http::RequestHeaderMap& request_headers);
  void ensureRequestID(Http::RequestHeaderMap& request_headers);
  void maybePreserveRequestIDInResponse(Http::ResponseHeaderMap& response_headers,
                                        const Http::RequestHeaderMap& request_headers);
  bool modRequestIDBy(const Http::RequestHeaderMap& request_headers, uint64_t& out, uint64_t mod);
  Envoy::RequestIDUtils::TraceStatus getTraceStatus(const Http::RequestHeaderMap& request_headers);
  void setTraceStatus(Http::RequestHeaderMap& request_headers,
                      const Envoy::RequestIDUtils::TraceStatus status);

private:
  // Reference to the random generator used to generate new request IDs
  Envoy::Runtime::RandomGenerator& random;

  // Byte on this position has predefined value of 4 for UUID4.
  static const int TRACE_BYTE_POSITION = 14;

  // Value of '9' is chosen randomly to distinguish between freshly generated uuid4 and the
  // one modified because we sample trace.
  static const char TRACE_SAMPLED = '9';

  // Value of 'a' is chosen randomly to distinguish between freshly generated uuid4 and the
  // one modified because we force trace.
  static const char TRACE_FORCED = 'a';

  // Value of 'b' is chosen randomly to distinguish between freshly generated uuid4 and the
  // one modified because of client trace.
  static const char TRACE_CLIENT = 'b';

  // Initial value for freshly generated uuid4.
  static const char NO_TRACE = '4';
};

} // namespace RequestIDUtils
} // namespace Extensions
} // namespace Envoy
