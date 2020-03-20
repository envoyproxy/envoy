#pragma once

#include "envoy/request_id_extension/request_id_extension.h"

#include "common/runtime/runtime_impl.h"

namespace Envoy {
namespace RequestIDExtension {

class UUIDUtils : public Envoy::RequestIDExtension::Utilities {
public:
  explicit UUIDUtils(Envoy::Runtime::RandomGenerator& random) : random_(random) {}

  void setRequestID(Http::RequestHeaderMap& request_headers);
  void ensureRequestID(Http::RequestHeaderMap& request_headers);
  void preserveRequestIDInResponse(Http::ResponseHeaderMap& response_headers,
                                   const Http::RequestHeaderMap& request_headers);
  bool modRequestIDBy(const Http::RequestHeaderMap& request_headers, uint64_t& out, uint64_t mod);
  Envoy::RequestIDExtension::TraceStatus
  getTraceStatus(const Http::RequestHeaderMap& request_headers);
  void setTraceStatus(Http::RequestHeaderMap& request_headers,
                      Envoy::RequestIDExtension::TraceStatus status);

private:
  // Reference to the random generator used to generate new request IDs
  Envoy::Runtime::RandomGenerator& random_;

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

} // namespace RequestIDExtension
} // namespace Envoy
