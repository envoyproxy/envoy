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
  absl::string_view key_equals_value, key, value;
  std::vector<std::pair<CelValue, CelValue>> key_value_pairs;
  std::cout << "********* query: " << query << std::endl;
  // loop while there are still equal signs in the query string "key=value&key=value&key=value..."
  while ((equal_pos = query.find('=', start)) != std::string::npos) {
    // look for ampersands in "key=value&key=value..."
    ampersand_pos = query.find('&', start);
    if (ampersand_pos != std::string::npos) {
      key_equals_value = query.substr(start, ampersand_pos-start);
      start = ampersand_pos+1;
    } else {
      key_equals_value = query.substr(start);
      start = query.length();
    }
    equal_pos = 0;
    key = "";
    value = "";
  std::cout << "********* key_equals_value: " << key_equals_value << std::endl;
    if ((equal_pos = key_equals_value.find('=')) != std::string::npos) {
      key = key_equals_value.substr(0, equal_pos);
      // skip if key is empty (e.g. "=value" or "=")
      if (key.length()==0) {
        continue;
      }
      // search key_value_pairs to see if key already exists
      bool duplicate_key_found = false;
      for (size_t i = 0; i < key_value_pairs.size(); ++i) {
        if ((key_value_pairs)[i].first.StringOrDie().value() == key) {
          duplicate_key_found = true;
          break;
        }
      }
      // skip duplicate keys
      if (duplicate_key_found) {
        continue;
      }
      value = key_equals_value.substr(equal_pos + 1);
  std::cout << "********* key: " << key << " value: " << value << std::endl;
      auto key_value_pair = std::make_pair
          (CelValue::CreateString(Protobuf::Arena::Create<std::string>(&arena_,
                                                                       key)),
           CelValue::CreateString(Protobuf::Arena::Create<std::string>(&arena_,
                                                                       value)));
      key_value_pairs.push_back(key_value_pair);
    }
  }
  // create ContainerBackedMapIml
  std::unique_ptr<CelMap> query_str_map =
      CreateContainerBackedMap(
          absl::Span<std::pair<CelValue, CelValue>>(key_value_pairs))
          .value();

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
