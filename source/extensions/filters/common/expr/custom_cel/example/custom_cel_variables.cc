#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_variables.h"

#include "source/common/http/utility.h"
#include "source/extensions/filters/common/expr/context.h"

#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_CEL {
namespace Example {

absl::optional<CelValue> CustomWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == Team) {
    return CelValue::CreateStringView("spirit");
  } else if (value == Protocol) {
    if (info_.protocol().has_value()) {
      return CelValue::CreateString(Protobuf::Arena::Create<std::string>(
          &arena_, Http::Utility::getProtocolString(info_.protocol().value())));
    }
  }
  return {};
}

// SourceWrapper extends PeerWrapper
// If SourceWrapper[key] is not found, then the base PeerWrapper[key] is returned.
absl::optional<CelValue> SourceWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == Description) {
    return CelValue::CreateString(
        Protobuf::Arena::Create<std::string>(&arena_, "description: has address, port values"));
  }
  return PeerWrapper::operator[](key);
}

// ExtendedRequestWrapper extends RequestWrapper
// If ExtendedRequestWrapper[key] is not found, then the base RequestWrapper[key] is returned.
absl::optional<CelValue> ExtendedRequestWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == Query) {
    absl::string_view path = request_header_map_->getPathValue();
    size_t query_offset = path.find('?');
    if (query_offset == absl::string_view::npos) {
      return {};
    }
    absl::string_view query = path.substr(query_offset + 1);
    if (!return_url_query_string_as_map_) {
      return CelValue::CreateString(Protobuf::Arena::Create<std::string>(&arena_, query));
    }
    return getMapFromQueryStr(path);
  }
  return RequestWrapper::operator[](key);
}

// getMapFromQueryStr
// converts std::map to CelMap
absl::optional<CelValue> ExtendedRequestWrapper::getMapFromQueryStr(absl::string_view url) const {
  Http::Utility::QueryParams query_params = Http::Utility::parseAndDecodeQueryString(url);
  std::vector<std::pair<CelValue, CelValue>> key_value_pairs;
  // create vector of key value pairs from QueryParams map
  for (const auto& [key, value] : query_params) {
    auto key_value_pair = std::make_pair(
        CelValue::CreateString(Protobuf::Arena::Create<std::string>(&arena_, key)),
        CelValue::CreateString(Protobuf::Arena::Create<std::string>(&arena_, value)));
    key_value_pairs.push_back(key_value_pair);
  }
  // create ContainerBackedMapImpl from vector of key value pairs
  std::unique_ptr<CelMap> params_unique_ptr =
      CreateContainerBackedMap(absl::Span<std::pair<CelValue, CelValue>>(key_value_pairs)).value();

  // transfer ownership of map from unique_ptr to arena
  CelMap* params_raw_ptr = params_unique_ptr.release();
  arena_.Own(params_raw_ptr);
  return CelValue::CreateMap(params_raw_ptr);
}

} // namespace Example
} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
