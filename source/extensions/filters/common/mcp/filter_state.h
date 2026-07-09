#pragma once

#include "envoy/json/json_object.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/json/json_loader.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/runtime/runtime_features.h"
#include "source/extensions/filters/common/mcp/constants.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Mcp {

inline absl::string_view metadataNamespace() {
  return Runtime::runtimeFeatureEnabled(
             "envoy.reloadable_features.mcp_filter_use_new_metadata_namespace")
             ? McpConstants::McpfilterNamespace
             : McpConstants::LegacyMetadataNamespace;
}

enum class Status { Ok, ParseError, NoMcp, NotJsonRpc, DuplicateKeys, BodyTooLarge };

inline absl::string_view statusToString(Status status) {
  switch (status) {
  case Status::Ok:
    return McpConstants::StatusValues::OK;
  case Status::ParseError:
    return McpConstants::StatusValues::PARSE_ERROR;
  case Status::NoMcp:
    return McpConstants::StatusValues::NO_MCP;
  case Status::NotJsonRpc:
    return McpConstants::StatusValues::NOT_JSONRPC;
  case Status::DuplicateKeys:
    return McpConstants::StatusValues::DUPLICATE_KEYS;
  case Status::BodyTooLarge:
    return McpConstants::StatusValues::BODY_TOO_LARGE;
  }
  return "unknown";
}

/**
 * FilterState object that stores parsed MCP request attributes.
 */
class FilterStateObject : public StreamInfo::FilterState::Object {
public:
  static constexpr absl::string_view FilterStateKey = "envoy.filters.http.mcp.request";

  FilterStateObject(std::string method, Json::ObjectSharedPtr json, bool is_mcp_request,
                    bool is_exceeding_limit = false, Status status = Status::Ok)
      : method_(std::move(method)), json_(std::move(json)), is_mcp_request_(is_mcp_request),
        is_exceeding_limit_(is_exceeding_limit), status_(status) {}

  FilterStateObject(std::string method, const Protobuf::Struct& proto_struct, bool is_mcp_request,
                    bool is_exceeding_limit = false, Status status = Status::Ok)
      : method_(std::move(method)), json_(Json::Factory::loadFromProtobufStruct(proto_struct)),
        is_mcp_request_(is_mcp_request), is_exceeding_limit_(is_exceeding_limit), status_(status) {}

  std::optional<std::string> serializeAsString() const override {
    if (json_ == nullptr || json_->empty()) {
      return std::nullopt;
    }
    return json_->asJsonString();
  }

  std::optional<absl::string_view> method() const {
    return method_.empty() ? std::nullopt : std::optional<absl::string_view>(method_);
  }

  const Json::ObjectSharedPtr& json() const { return json_; }

  bool isMcpRequest() const { return is_mcp_request_; }
  bool isExceedingLimit() const { return is_exceeding_limit_; }
  Status status() const { return status_; }

private:
  std::string method_;
  Json::ObjectSharedPtr json_;
  bool is_mcp_request_;
  bool is_exceeding_limit_;
  Status status_;
};

} // namespace Mcp
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
