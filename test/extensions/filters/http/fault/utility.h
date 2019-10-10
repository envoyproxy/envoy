
#include "envoy/config/filter/http/fault/v2/fault.pb.h"

#include <string>

#include "test/test_common/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Fault {

envoy::config::filter::http::fault::v2::HTTPFault
convertYamlStrToProtoConfig(const std::string& yaml) {
  envoy::config::filter::http::fault::v2::HTTPFault fault;
  TestUtility::loadFromYaml(yaml, fault);
  return fault;
}

} // namespace Fault
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
