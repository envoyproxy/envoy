#include "source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib/field_checker.h"

#include "source/common/protobuf/protobuf.h"

#include "proto_processing_lib/proto_scrubber/field_checker_interface.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

FieldCheckResults FieldChecker::CheckField(const std::vector<std::string>&,
                                           const Protobuf::Field*) const {
  return FieldCheckResults::kInclude;
}

FieldCheckResults FieldChecker::CheckType(const Protobuf::Type*) const {
  return FieldCheckResults::kInclude;
}

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
