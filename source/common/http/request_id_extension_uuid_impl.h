#pragma once

#include "envoy/http/request_id_extension.h"

#include "common/runtime/runtime_impl.h"

namespace Envoy {
namespace Http {

// UUIDRequestIDExtension is the default implementation if no other extension is explicitly
// configured.
class UUIDRequestIDExtension : public RequestIDExtension {
public:
  explicit UUIDRequestIDExtension(Envoy::Runtime::RandomGenerator& random) : random_(random) {}

  void set(RequestHeaderMap& request_headers, bool force) override;
  void setInResponse(ResponseHeaderMap& response_headers,
                     const RequestHeaderMap& request_headers) override;
  bool modBy(const RequestHeaderMap& request_headers, uint64_t& out, uint64_t mod) override;
  TraceStatus getTraceStatus(const RequestHeaderMap& request_headers) override;
  void setTraceStatus(RequestHeaderMap& request_headers, TraceStatus status) override;

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

} // namespace Http
} // namespace Envoy
