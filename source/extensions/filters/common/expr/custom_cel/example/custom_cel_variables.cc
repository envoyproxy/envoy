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
    return getMapFromQueryStr(query);
  }
  return RequestWrapper::operator[](key);
}

absl::optional<CelValue> ExtendedRequestWrapper::getMapFromQueryStr(absl::string_view query) const {
  size_t equal_pos = 0, ampersand_pos = 0, start = 0;
  absl::string_view key_equals_value = "", key = "", value = "";
  std::vector<std::pair<CelValue, CelValue>> key_value_pairs;
  absl::flat_hash_map<absl::string_view, absl::string_view> parameters_map;
  // loop while there are still equal signs in the query string "key=value&key=value&key=value..."
  while (query.find('=', start) != std::string::npos) {
    // look for ampersands in "key=value&key=value..."
    ampersand_pos = query.find('&', start);
    if (ampersand_pos != std::string::npos) {
      key_equals_value = query.substr(start, ampersand_pos - start);
      start = ampersand_pos + 1;
    } else {
      key_equals_value = query.substr(start);
      start = query.length();
    }
    if ((equal_pos = key_equals_value.find('=')) != std::string::npos) {
      key = key_equals_value.substr(0, equal_pos);
      // continue if key is empty (e.g. "=value" or "=")
      if (key.length() == 0) {
        continue;
      }
      // continue if key already exists and this is a duplicate
      if (parameters_map.contains(key)) {
        continue;
      }
      value = key_equals_value.substr(equal_pos + 1);
      // store key value pair twice, in the parameters hash map and the vector of key value pairs
      parameters_map[key] = value;
      auto key_value_pair = std::make_pair(
          CelValue::CreateString(Protobuf::Arena::Create<std::string>(&arena_, key)),
          CelValue::CreateString(Protobuf::Arena::Create<std::string>(&arena_, value)));
      key_value_pairs.push_back(key_value_pair);
    }
  }
  // create ContainerBackedMapImpl from vector of key value pairs
  std::unique_ptr<CelMap> query_str_map =
      CreateContainerBackedMap(absl::Span<std::pair<CelValue, CelValue>>(key_value_pairs)).value();

  // transfer ownership of map from unique_ptr to arena
  CelMap* query_str_map_raw_ptr = query_str_map.release();
  arena_.Own(query_str_map_raw_ptr);
  return CelValue::CreateMap(query_str_map_raw_ptr);
}

} // namespace Example
} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
