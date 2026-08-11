#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "source/extensions/filters/http/ai_protocol_manager/json_with_ext_buf.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

// Declarative, type-safe, and recursive JSON schema definition.
//
// Supports primitive types (String, Number, Integer, Boolean, Null, Any), composite types
// (Object, Array), and polymorphic union types (OneOf).
//
// Allows explicit marking of `.offloadable()` string fields that accept buffer-offloaded
// `ExternalRef` nodes from `JsonWithExtBuf`, while rejecting offload nodes on non-offloadable
// metadata fields (e.g. `model`, `role`, `type`).
class Schema {
public:
  enum class Type {
    String,
    Number,
    Integer,
    Boolean,
    Object,
    Array,
    Null,
    Any,
    OneOf,
  };

  struct Property {
    Property(std::string n, Schema s);
    Property(const char* n, Schema s);

    std::string name;
    std::shared_ptr<Schema> schema;
  };

  using CustomValidator = std::function<absl::Status(const nlohmann::json&)>;

  Schema(const Schema&) = default;
  Schema(Schema&&) noexcept = default;
  Schema& operator=(const Schema&) = default;
  Schema& operator=(Schema&&) noexcept = default;

  // Static factory methods for declarative schema construction:
  static Schema string();
  static Schema enumString(std::vector<std::string> allowed_values);
  static Schema number();
  static Schema integer();
  static Schema boolean();
  static Schema null();
  static Schema any();
  static Schema object(std::initializer_list<Property> properties);
  static Schema object(std::vector<Property> properties);
  static Schema array(Schema element_schema);
  static Schema oneOf(std::initializer_list<Schema> candidates);
  static Schema oneOf(std::vector<Schema> candidates);

  // Fluent modifiers:
  Schema& required(bool req = true) {
    is_required_ = req;
    return *this;
  }
  Schema& optional() {
    is_required_ = false;
    return *this;
  }
  bool isRequired() const { return is_required_; }

  Schema& offloadable(bool off = true) {
    is_offloadable_ = off;
    return *this;
  }
  bool isOffloadable() const { return is_offloadable_; }

  Schema& range(double min_val, double max_val) {
    min_number_ = min_val;
    max_number_ = max_val;
    return *this;
  }
  Schema& min(int64_t min_val) {
    min_integer_ = min_val;
    return *this;
  }
  Schema& max(int64_t max_val) {
    max_integer_ = max_val;
    return *this;
  }

  Schema& allowUnknownFields(bool allow = true) {
    allow_unknown_fields_ = allow;
    return *this;
  }
  bool allowsUnknownFields() const { return allow_unknown_fields_; }

  Schema& customValidator(CustomValidator validator) {
    custom_validator_ = std::move(validator);
    return *this;
  }

  // Validation:
  absl::Status validate(const nlohmann::json& json, absl::string_view path = "/") const;
  absl::Status validate(const JsonWithExtBuf& payload) const {
    return validate(payload.json(), "/");
  }

  // Introspection & Reflection accessors:
  Type type() const { return type_; }
  const std::vector<Property>& properties() const { return properties_; }
  const Schema* elementSchema() const { return element_schema_.get(); }
  const std::vector<Schema>& oneOfCandidates() const { return one_of_candidates_; }
  const std::vector<std::string>& allowedValues() const { return allowed_values_; }

private:
  explicit Schema(Type type) : type_(type) {}

  Type type_{Type::Any};
  bool is_required_{false};
  bool is_offloadable_{false};
  bool allow_unknown_fields_{true};

  std::vector<std::string> allowed_values_;
  std::optional<double> min_number_;
  std::optional<double> max_number_;
  std::optional<int64_t> min_integer_;
  std::optional<int64_t> max_integer_;

  std::vector<Property> properties_;
  std::shared_ptr<Schema> element_schema_;
  std::vector<Schema> one_of_candidates_;
  CustomValidator custom_validator_;
};

// Request schema wrapper around the root Schema.
class RequestSchema {
public:
  RequestSchema() : root_schema_(Schema::object({}).allowUnknownFields(true)) {}
  explicit RequestSchema(Schema root_schema) : root_schema_(std::move(root_schema)) {}

  const Schema& rootSchema() const { return root_schema_; }

  absl::Status validate(const JsonWithExtBuf& payload) const {
    return root_schema_.validate(payload);
  }
  absl::Status validate(const nlohmann::json& json) const {
    return root_schema_.validate(json, "/");
  }

private:
  Schema root_schema_;
};

// Response schema definition. Left empty / placeholder for now as response
// schema parsing and validation will be added in a later change.
class ResponseSchema {
public:
  ResponseSchema() = default;
  explicit ResponseSchema(Schema root_schema) : root_schema_(std::move(root_schema)) {}

  const std::optional<Schema>& rootSchema() const { return root_schema_; }

private:
  std::optional<Schema> root_schema_;
};

// Container holding the pair of request and response schemas for an AI endpoint protocol.
class PayloadSchema {
public:
  explicit PayloadSchema(RequestSchema request_schema,
                         ResponseSchema response_schema = ResponseSchema())
      : request_schema_(std::move(request_schema)), response_schema_(std::move(response_schema)) {}

  const RequestSchema& requestSchema() const { return request_schema_; }
  const ResponseSchema& responseSchema() const { return response_schema_; }

  absl::Status validateRequest(const JsonWithExtBuf& payload) const {
    return request_schema_.validate(payload);
  }
  absl::Status validateRequest(const nlohmann::json& json) const {
    return request_schema_.validate(json);
  }

private:
  RequestSchema request_schema_;
  ResponseSchema response_schema_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
