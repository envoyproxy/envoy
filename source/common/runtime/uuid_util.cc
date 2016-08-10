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

bool UuidUtils::isTraceableUuid(const std::string& uuid) {
  return uuid.length() == Runtime::RandomGeneratorImpl::UUID_LENGTH &&
         uuid[TRACE_BYTE_POSITION] == TRACE_BYTE_VALUE;
}

bool UuidUtils::setTraceableUuid(std::string& uuid) {
  if (uuid.length() != Runtime::RandomGeneratorImpl::UUID_LENGTH) {
    return false;
  }

  uuid[TRACE_BYTE_POSITION] = TRACE_BYTE_VALUE;

  return true;
}
