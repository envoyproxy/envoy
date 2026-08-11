#include "source/extensions/filters/http/ai_protocol_manager/schema.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

namespace {

std::string jsonTypeName(const nlohmann::json& json) {
  if (JsonWithExtBuf::isExternalRef(json)) {
    return "external_ref_string";
  }
  switch (json.type()) {
  case nlohmann::json::value_t::null:
    return "null";
  case nlohmann::json::value_t::object:
    return "object";
  case nlohmann::json::value_t::array:
    return "array";
  case nlohmann::json::value_t::string:
    return "string";
  case nlohmann::json::value_t::boolean:
    return "boolean";
  case nlohmann::json::value_t::number_integer:
  case nlohmann::json::value_t::number_unsigned:
    return "integer";
  case nlohmann::json::value_t::number_float:
    return "number";
  case nlohmann::json::value_t::binary:
    return "binary";
  case nlohmann::json::value_t::discarded:
    return "discarded";
  }
  return "unknown";
}

std::string makeChildPath(absl::string_view parent_path, absl::string_view child_key) {
  if (parent_path.empty()) {
    return std::string(child_key);
  }
  return absl::StrCat(parent_path, ".", child_key);
}

std::string makeArrayChildPath(absl::string_view parent_path, size_t index) {
  if (parent_path.empty()) {
    return absl::StrCat("[", index, "]");
  }
  return absl::StrCat(parent_path, "[", index, "]");
}

} // namespace

Schema::Property::Property(std::string n, Schema s)
    : name(std::move(n)), schema(std::make_shared<Schema>(std::move(s))) {}

Schema::Property::Property(const char* n, Schema s)
    : name(n), schema(std::make_shared<Schema>(std::move(s))) {}

Schema Schema::string() { return Schema(Type::String); }

Schema Schema::enumString(std::vector<std::string> allowed_values) {
  Schema s(Type::String);
  s.allowed_values_ = std::move(allowed_values);
  return s;
}

Schema Schema::number() { return Schema(Type::Number); }

Schema Schema::integer() { return Schema(Type::Integer); }

Schema Schema::boolean() { return Schema(Type::Boolean); }

Schema Schema::null() { return Schema(Type::Null); }

Schema Schema::any() { return Schema(Type::Any); }

Schema Schema::object(std::initializer_list<Property> properties) {
  Schema s(Type::Object);
  s.properties_ = std::vector<Property>(properties);
  return s;
}

Schema Schema::object(std::vector<Property> properties) {
  Schema s(Type::Object);
  s.properties_ = std::move(properties);
  return s;
}

Schema Schema::array(Schema element_schema) {
  Schema s(Type::Array);
  s.element_schema_ = std::make_shared<Schema>(std::move(element_schema));
  return s;
}

Schema Schema::oneOf(std::initializer_list<Schema> candidates) {
  Schema s(Type::OneOf);
  s.one_of_candidates_ = std::vector<Schema>(candidates);
  return s;
}

Schema Schema::oneOf(std::vector<Schema> candidates) {
  Schema s(Type::OneOf);
  s.one_of_candidates_ = std::move(candidates);
  return s;
}

absl::Status Schema::validate(const nlohmann::json& json, absl::string_view path) const {
  switch (type_) {
  case Type::String: {
    if (JsonWithExtBuf::isExternalRef(json)) {
      if (!is_offloadable_) {
        return absl::InvalidArgumentError(
            absl::StrCat("field '", path, "' cannot be offloaded to external buffer"));
      }
      break;
    }
    if (!json.is_string()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "field '", path, "' has invalid type: expected string, got ", jsonTypeName(json)));
    }
    if (!allowed_values_.empty()) {
      const std::string val = json.get<std::string>();
      if (std::find(allowed_values_.begin(), allowed_values_.end(), val) == allowed_values_.end()) {
        return absl::InvalidArgumentError(
            absl::StrCat("field '", path, "' has invalid value: '", val, "'"));
      }
    }
    break;
  }
  case Type::Number: {
    if (!json.is_number()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "field '", path, "' has invalid type: expected number, got ", jsonTypeName(json)));
    }
    const double val =
        json.is_number_float() ? json.get<double>() : static_cast<double>(json.get<int64_t>());
    if (min_number_.has_value() && val < *min_number_) {
      return absl::InvalidArgumentError(
          absl::StrCat("field '", path, "' value ", val, " is less than minimum ", *min_number_));
    }
    if (max_number_.has_value() && val > *max_number_) {
      return absl::InvalidArgumentError(absl::StrCat("field '", path, "' value ", val,
                                                     " is greater than maximum ", *max_number_));
    }
    break;
  }
  case Type::Integer: {
    if (!json.is_number_integer() && !json.is_number_unsigned()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "field '", path, "' has invalid type: expected integer, got ", jsonTypeName(json)));
    }
    const int64_t val = json.get<int64_t>();
    if (min_integer_.has_value() && val < *min_integer_) {
      return absl::InvalidArgumentError(
          absl::StrCat("field '", path, "' value ", val, " is less than minimum ", *min_integer_));
    }
    if (max_integer_.has_value() && val > *max_integer_) {
      return absl::InvalidArgumentError(absl::StrCat("field '", path, "' value ", val,
                                                     " is greater than maximum ", *max_integer_));
    }
    break;
  }
  case Type::Boolean: {
    if (!json.is_boolean()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "field '", path, "' has invalid type: expected boolean, got ", jsonTypeName(json)));
    }
    break;
  }
  case Type::Null: {
    if (!json.is_null()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "field '", path, "' has invalid type: expected null, got ", jsonTypeName(json)));
    }
    break;
  }
  case Type::Any: {
    break;
  }
  case Type::Object: {
    if (!json.is_object()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "field '", path, "' has invalid type: expected object, got ", jsonTypeName(json)));
    }
    for (const Property& prop : properties_) {
      auto it = json.find(prop.name);
      const std::string child_path = makeChildPath(path, prop.name);
      if (it == json.end()) {
        if (prop.schema != nullptr && prop.schema->isRequired()) {
          return absl::InvalidArgumentError(absl::StrCat("missing required field: ", child_path));
        }
      } else if (prop.schema != nullptr) {
        auto status = prop.schema->validate(it.value(), child_path);
        if (!status.ok()) {
          return status;
        }
      }
    }
    if (!allow_unknown_fields_) {
      for (auto it = json.begin(); it != json.end(); ++it) {
        bool found = false;
        for (const Property& prop : properties_) {
          if (prop.name == it.key()) {
            found = true;
            break;
          }
        }
        if (!found) {
          return absl::InvalidArgumentError(
              absl::StrCat("unknown field: ", makeChildPath(path, it.key())));
        }
      }
    }
    break;
  }
  case Type::Array: {
    if (!json.is_array()) {
      return absl::InvalidArgumentError(absl::StrCat(
          "field '", path, "' has invalid type: expected array, got ", jsonTypeName(json)));
    }
    if (min_integer_.has_value() && static_cast<int64_t>(json.size()) < *min_integer_) {
      return absl::InvalidArgumentError(absl::StrCat("field '", path, "' array size ", json.size(),
                                                     " is less than minimum ", *min_integer_));
    }
    if (max_integer_.has_value() && static_cast<int64_t>(json.size()) > *max_integer_) {
      return absl::InvalidArgumentError(absl::StrCat("field '", path, "' array size ", json.size(),
                                                     " is greater than maximum ", *max_integer_));
    }
    if (element_schema_ != nullptr) {
      for (size_t i = 0; i < json.size(); ++i) {
        const std::string child_path = makeArrayChildPath(path, i);
        auto status = element_schema_->validate(json[i], child_path);
        if (!status.ok()) {
          return status;
        }
      }
    }
    break;
  }
  case Type::OneOf: {
    bool matched = false;
    std::vector<std::string> failure_reasons;
    for (const Schema& candidate : one_of_candidates_) {
      auto status = candidate.validate(json, path);
      if (status.ok()) {
        matched = true;
        break;
      }
      failure_reasons.push_back(std::string(status.message()));
    }
    if (!matched) {
      return absl::InvalidArgumentError(absl::StrCat("field '", path,
                                                     "' did not match any allowed oneof schemas: [",
                                                     absl::StrJoin(failure_reasons, "; "), "]"));
    }
    break;
  }
  }

  if (custom_validator_) {
    auto status = custom_validator_(json);
    if (!status.ok()) {
      return absl::InvalidArgumentError(
          absl::StrCat("field '", path, "' failed validation: ", status.message()));
    }
  }

  return absl::OkStatus();
}

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
