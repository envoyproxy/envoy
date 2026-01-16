#pragma once

#include "envoy/json/json_object.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/json/json_loader.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Mcp {

/**
 * FilterState object that stores parsed MCP request attributes.
 */
class FilterStateObject : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view FilterStateKey = "envoy.filters.http.mcp.request";

  FilterStateObject(std::string method, Json::ObjectSharedPtr json)
      : method_(std::move(method)), json_(std::move(json)) {}

  FilterStateObject(std::string method, const Protobuf::Struct& proto_struct)
      : method_(std::move(method)), json_(Json::Factory::loadFromProtobufStruct(proto_struct)) {}

  absl::optional<std::string> serializeAsString() const override {
    if (json_ == nullptr || json_->empty()) {
      return absl::nullopt;
    }
    return json_->asJsonString();
  }

  absl::optional<absl::string_view> method() const {
    return method_.empty() ? absl::nullopt : absl::optional<absl::string_view>(method_);
  }

  const Json::ObjectSharedPtr& json() const { return json_; }

private:
  std::string method_;
  Json::ObjectSharedPtr json_;
};

} // namespace Mcp
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
