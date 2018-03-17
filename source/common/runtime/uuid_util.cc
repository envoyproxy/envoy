#include "common/runtime/uuid_util.h"

#include <cstdint>
#include <string>

#include "common/common/utility.h"
#include "common/runtime/runtime_impl.h"

namespace Envoy {
bool UuidUtils::uuidModBy(const std::string& uuid, uint64_t& out, uint64_t mod) {
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

UuidTraceStatus UuidUtils::isTraceableUuid(const std::string& uuid) {
  if (uuid.length() != Runtime::RandomGeneratorImpl::UUID_LENGTH) {
    return UuidTraceStatus::NoTrace;
  }

  switch (uuid[TRACE_BYTE_POSITION]) {
  case TRACE_FORCED:
    return UuidTraceStatus::Forced;
  case TRACE_SAMPLED:
    return UuidTraceStatus::Sampled;
  case TRACE_CLIENT:
    return UuidTraceStatus::Client;
  default:
    return UuidTraceStatus::NoTrace;
  }
}

bool UuidUtils::setTraceableUuid(std::string& uuid, UuidTraceStatus trace_status) {
  if (uuid.length() != Runtime::RandomGeneratorImpl::UUID_LENGTH) {
    return false;
  }

  switch (trace_status) {
  case UuidTraceStatus::Forced:
    uuid[TRACE_BYTE_POSITION] = TRACE_FORCED;
    break;
  case UuidTraceStatus::Client:
    uuid[TRACE_BYTE_POSITION] = TRACE_CLIENT;
    break;
  case UuidTraceStatus::Sampled:
    uuid[TRACE_BYTE_POSITION] = TRACE_SAMPLED;
    break;
  case UuidTraceStatus::NoTrace:
    uuid[TRACE_BYTE_POSITION] = NO_TRACE;
    break;
  }

  return true;
}
} // namespace Envoy
