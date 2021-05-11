#include "extensions/filters/http/experimental_compiler_features/filter.h"

#include <string>

#include "envoy/extensions/filters/http/experimental_compiler_features/v3/experimental_compiler_features.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExperimentalCompilerFeatures {

FilterConfigImpl::FilterConfigImpl(const std::string& key, const std::string& value,
                                   bool associative_container_use_contains,
                                   bool enum_members_in_scope, bool str_starts_with,
                                   bool str_ends_with, const std::string& enum_value,
                                   const std::string& start_end_string,
                                   const std::string& associative_container_string)
    : key_(key), val_(value),
      associative_container_use_contains_(associative_container_use_contains),
      enum_members_in_scope_(enum_members_in_scope), str_starts_with_(str_starts_with),
      str_ends_with_(str_ends_with), enum_value_(enum_value), start_end_string_(start_end_string),
      associative_container_string_(associative_container_string) {}

Filter::Filter(const std::shared_ptr<FilterConfig>& config) : config_(config) {}

const std::string& FilterConfigImpl::key() const { return key_; }
const std::string& FilterConfigImpl::val() const { return val_; }

bool FilterConfigImpl::associativeContainerUseContains() const {
  return associative_container_use_contains_;
}
bool FilterConfigImpl::enumMembersInScope() const { return enum_members_in_scope_; }
bool FilterConfigImpl::strStartsWith() const { return str_starts_with_; }
bool FilterConfigImpl::strEndsWith() const { return str_ends_with_; }

// These vars are used for testing only.
const std::string& FilterConfigImpl::enumValue() const { return enum_value_; }
const std::string& FilterConfigImpl::startEndString() const { return start_end_string_; }
const std::string& FilterConfigImpl::associativeContainerString() const {
  return associative_container_string_;
}

const Http::LowerCaseString Filter::headerKey() const {
  return Http::LowerCaseString(config_->key());
}

const std::string Filter::headerValue() const { return config_->val(); }

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap& headers, bool) {

  // add a header
  headers.addCopy(headerKey(), headerValue());

// C++20 features if enabled
#if defined(__cpp_lib_generic_associative_lookup)
  if (config_->associativeContainerUseContains()) {
    std::map<std::string, std::string> map{{"val1", "key1"}, {"val2", "key2"}, {"val3", "key3"}};
    std::set<std::string> set{"val1", "val2", "val3"};

    std::string str_to_find = config_->associativeContainerString();
    if (map.contains(str_to_find) && set.contains(str_to_find)) {
      headers.addCopy(Http::LowerCaseString("x-cpp20-associative-container-use-contains"),
                      "contains:" + str_to_find);
    } else {
      headers.addCopy(Http::LowerCaseString("x-cpp20-associative-container-use-contains"),
                      "does not contains:" + str_to_find);
    }
  }
#endif

#if defined(__cpp_using_enum)
  if (config_->enumMembersInScope()) {
    int enum_index = -1;
    if (config_->enumValue() == "red") {
      enum_index = 0;
    } else if (config_->enumValue() == "green") {
      enum_index = 1;
    } else if (config_->enumValue() == "blue") {
      enum_index = 2;
    } else if (config_->enumValue() == "alpha") {
      enum_index = 3;
    } else {
      headers.addCopy(Http::LowerCaseString("x-cpp20-enum-members-in-scope"), "ilegal value");
    }

    if (enum_index >= 0) {
      enum class rgba_color_channel { red, green, blue, alpha };

      using enum rgba_color_channel;

      rgba_color_channel my_channel{(rgba_color_channel)enum_index};

      switch (my_channel) {

      case red:
        headers.addCopy(Http::LowerCaseString("x-cpp20-enum-members-in-scope"), "red");
        break;
      case green:
        headers.addCopy(Http::LowerCaseString("x-cpp20-enum-members-in-scope"), "green");
        break;
      case blue:
        headers.addCopy(Http::LowerCaseString("x-cpp20-enum-members-in-scope"), "blue");
        break;
      case alpha:
        headers.addCopy(Http::LowerCaseString("x-cpp20-enum-members-in-scope"), "alpha");
        break;
      default:
        headers.addCopy(Http::LowerCaseString("x-cpp20-enum-members-in-scope"),
                        "invalid enum value");
        break;
      }
    }
  }
#endif

#if defined(__cpp_lib_starts_ends_with)
  if (config_->strStartsWith()) {
    std::string foo_header_val = config_->startEndString();

    if (foo_header_val.starts_with("server:latam_")) { // true
      headers.addCopy(Http::LowerCaseString("x-cpp20-str-starts-with"), "latam");
    } else {
      headers.addCopy(Http::LowerCaseString("x-cpp20-str-starts-with"), "invalid");
    }
  }

  if (config_->strEndsWith()) {
    std::string foo_allowed_token = "bfFw1DCsranQ6x2zZKYGYVc0zqW99UB05IZPuQjv";
    std::string foo_header_val = config_->startEndString();

    if (foo_header_val.ends_with("_tk:" + foo_allowed_token)) { // true
      headers.addCopy(Http::LowerCaseString("x-cpp20-str-ends-with"), "allowed");
    } else {
      headers.addCopy(Http::LowerCaseString("x-cpp20-str-ends-with"), "not allowed");
    }
  }
#endif

  return Http::FilterHeadersStatus::Continue;
}

} // namespace ExperimentalCompilerFeatures
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
