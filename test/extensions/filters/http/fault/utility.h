#pragma once

#include <string>

#include "envoy/extensions/filters/http/fault/v3alpha/fault.pb.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

inline envoy::extensions::filters::http::fault::v3alpha::HTTPFault
convertYamlStrToProtoConfig(const std::string& yaml) {
  envoy::extensions::filters::http::fault::v3alpha::HTTPFault fault;
  TestUtility::loadFromYaml(yaml, fault);
  return fault;
}

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
