#pragma once

#include "envoy/json/json_object.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/json/json_loader.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/runtime/runtime_features.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Mcp {

constexpr absl::string_view NewMetadataNamespace = "envoy.filters.http.mcp";
constexpr absl::string_view LegacyMetadataNamespace = "mcp_proxy";

inline absl::string_view metadataNamespace() {
  return Runtime::runtimeFeatureEnabled(
             "envoy.reloadable_features.mcp_filter_use_new_metadata_namespace")
             ? NewMetadataNamespace
             : LegacyMetadataNamespace;
}

/**
 * FilterState object that stores parsed MCP request attributes.
 */
class FilterStateObject : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view FilterStateKey = "envoy.filters.http.mcp.request";

  FilterStateObject(std::string method, Json::ObjectSharedPtr json, bool is_mcp_request)
      : method_(std::move(method)), json_(std::move(json)), is_mcp_request_(is_mcp_request) {}

  FilterStateObject(std::string method, const Protobuf::Struct& proto_struct, bool is_mcp_request)
      : method_(std::move(method)), json_(Json::Factory::loadFromProtobufStruct(proto_struct)),
        is_mcp_request_(is_mcp_request) {}

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

  bool isMcpRequest() const { return is_mcp_request_; }

private:
  std::string method_;
  Json::ObjectSharedPtr json_;
  bool is_mcp_request_;
};

} // namespace Mcp
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
