#pragma once

#include "envoy/json/json_object.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/json/json_loader.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {

/**
 * FilterState object that stores parsed MCP request attributes.
 */
class McpFilterStateObject : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view FilterStateKey = "envoy.filters.http.mcp.request";

  McpFilterStateObject(std::string method, Json::ObjectSharedPtr json)
      : method_(std::move(method)), json_(std::move(json)) {}

  McpFilterStateObject(std::string method, const Protobuf::Struct& proto_struct)
      : method_(std::move(method)), json_(Json::Factory::loadFromProtobufStruct(proto_struct)) {}

  bool hasFieldSupport() const override { return true; }

  FieldType getField(absl::string_view field_name) const override {
    if (json_ != nullptr && json_->hasObject(std::string(field_name))) {
      auto result = json_->getString(std::string(field_name), "");
      if (result.ok() && !result.value().empty()) {
        return result.value();
      }
    }
    return absl::monostate{};
  }

  absl::optional<std::string> serializeAsString() const override {
    if (json_ == nullptr || json_->empty()) {
      return absl::nullopt;
    }
    return json_->asJsonString();
  }

  absl::optional<absl::string_view> method() const {
    return method_.empty() ? absl::nullopt : absl::optional<absl::string_view>(method_);
  }

  bool hasField(absl::string_view field_name) const {
    return json_ != nullptr && json_->hasObject(std::string(field_name));
  }

  const Json::ObjectSharedPtr& json() const { return json_; }

private:
  std::string method_;
  Json::ObjectSharedPtr json_;
};

} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
