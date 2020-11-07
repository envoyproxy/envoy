#pragma once

#include <string>

#include "envoy/extensions/filters/http/fault/v3/fault.pb.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

inline envoy::extensions::filters::http::fault::v3::HTTPFault
convertYamlStrToProtoConfig(const std::string& yaml) {
  envoy::extensions::filters::http::fault::v3::HTTPFault fault;
  TestUtility::loadFromYaml(yaml, fault);
  return fault;
}

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
