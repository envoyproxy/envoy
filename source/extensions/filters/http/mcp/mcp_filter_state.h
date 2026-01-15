#pragma once

#include "envoy/stream_info/filter_state.h"

#include "absl/container/flat_hash_map.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

/**
 * FilterState object that stores parsed MCP request attributes.
 * The method field is stored as a direct struct member for efficient access.
 * Other fields (jsonrpc, id, params.*) are stored in a flexible map.
 */
class McpFilterStateObject : public StreamInfo::FilterState::Object {
public:
  // FilterStateKey is used to store the FilterState::Object in the FilterState.
  static constexpr absl::string_view FilterStateKey = "envoy.filters.http.mcp.request";

  using FieldMap = absl::flat_hash_map<std::string, std::string>;

  /**
   * Builder for efficient construction from parser output.
   */
  class Builder {
  public:
    Builder& setMethod(std::string method) {
      method_ = std::move(method);
      return *this;
    }

    Builder& setId(std::string id) {
      fields_["id"] = std::move(id);
      return *this;
    }

    Builder& setJsonRpc(std::string version) {
      fields_["jsonrpc"] = std::move(version);
      return *this;
    }

    Builder& addField(std::string key, std::string value) {
      fields_[std::move(key)] = std::move(value);
      return *this;
    }

    std::shared_ptr<McpFilterStateObject> build() {
      return std::make_shared<McpFilterStateObject>(std::move(method_), std::move(fields_));
    }

  private:
    std::string method_;
    FieldMap fields_;
  };

  McpFilterStateObject(std::string method, FieldMap&& fields)
      : method_(std::move(method)), fields_(std::move(fields)) {
    if (!method_.empty()) {
      fields_["method"] = method_;
    }
  }

  bool hasFieldSupport() const override { return true; }

  FieldType getField(absl::string_view field_name) const override {
    auto it = fields_.find(field_name);
    if (it != fields_.end()) {
      return absl::string_view(it->second);
    }
    return absl::monostate{};
  }

  absl::optional<std::string> serializeAsString() const override {
    if (fields_.empty()) {
      return absl::nullopt;
    }
    nlohmann::json j = nlohmann::json::object();
    for (const auto& [key, value] : fields_) {
      j[key] = value;
    }
    return j.dump();
  }

  absl::optional<absl::string_view> method() const {
    return method_.empty() ? absl::nullopt : absl::optional<absl::string_view>(method_);
  }
  absl::optional<absl::string_view> id() const {
    auto it = fields_.find("id");
    return it != fields_.end() ? absl::optional<absl::string_view>(it->second) : absl::nullopt;
  }

  absl::optional<absl::string_view> jsonrpc() const {
    auto it = fields_.find("jsonrpc");
    return it != fields_.end() ? absl::optional<absl::string_view>(it->second) : absl::nullopt;
  }

  bool hasField(absl::string_view field_name) const {
    return fields_.find(field_name) != fields_.end();
  }

  const FieldMap& fields() const { return fields_; }

private:
  std::string method_;
  FieldMap fields_;
};

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
