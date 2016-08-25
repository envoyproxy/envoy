#include "runtime_impl.h"
#include "uuid_util.h"

#include "common/common/utility.h"

bool UuidUtils::uuidModBy(const std::string& uuid, uint16_t& out, uint16_t mod) {
  if (uuid.length() < 8) {
    return false;
  }

  uint64_t value;
  if (!StringUtil::atoul(uuid.substr(0, 8).c_str(), value, 16)) {
    return false;
  }

  out = value % mod;
  return true;
}

TraceDecision UuidUtils::isTraceableUuid(const std::string& uuid) {
  if (uuid.length() != Runtime::RandomGeneratorImpl::UUID_LENGTH) {
    return {false, Optional<TraceReason>()};
  }

  switch (uuid[TRACE_BYTE_POSITION]) {
  case TRACE_FORCED:
    return {true, Optional<TraceReason>(TraceReason::Forced)};
  case TRACE_SAMPLED:
    return {true, Optional<TraceReason>(TraceReason::Sampled)};
  case TRACE_CLIENT:
    return {true, Optional<TraceReason>(TraceReason::Client)};
  default:
    return {false, Optional<TraceReason>()};
  }
}

bool UuidUtils::setTraceableUuid(std::string& uuid, TraceReason trace_reason) {
  if (uuid.length() != Runtime::RandomGeneratorImpl::UUID_LENGTH) {
    return false;
  }

  switch (trace_reason) {
  case TraceReason::Forced:
    uuid[TRACE_BYTE_POSITION] = TRACE_FORCED;
    break;
  case TraceReason::Client:
    uuid[TRACE_BYTE_POSITION] = TRACE_CLIENT;
    break;
  case TraceReason::Sampled:
    uuid[TRACE_BYTE_POSITION] = TRACE_SAMPLED;
    break;
  }

  return true;
}
