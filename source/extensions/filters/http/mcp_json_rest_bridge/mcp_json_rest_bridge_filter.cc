#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

using ::nlohmann::json;

constexpr absl::string_view kLatestSupportedMcpVersion = "2025-11-25";
constexpr absl::string_view kServerName = "mcp-json-rest-bridge";

absl::StatusOr<int> getSessionId(const json& json_rpc) {
  if (auto it = json_rpc.find("id"); it != json_rpc.end()) {
    if (it->is_number_integer()) {
      return it->get<int>();
    }
    if (it->is_string()) {
      int int_id;
      if (absl::SimpleAtoi(it->get<std::string>(), &int_id)) {
        return int_id;
      }
    }
    return absl::InvalidArgumentError("JSON-RPC request ID is not an integer or a numeric string.");
  }
  return absl::InvalidArgumentError("JSON-RPC request does not have an ID.");
}

json generateInitializeResponse(int session_id) {
  json ret;
  ret["jsonrpc"] = "2.0";
  ret["id"] = session_id;

  json result;
  result["protocolVersion"] = kLatestSupportedMcpVersion;
  result["capabilities"]["tools"]["listChanged"] = false;
  result["serverInfo"]["name"] = kServerName;
  result["serverInfo"]["version"] = "1.0.0";
  ret["result"] = result;
  return ret;
}

json generateErrorJsonResponse(int error_code, absl::string_view error_message) {
  json error = json::object();
  error["code"] = error_code;
  error["message"] = error_message;
  return error;
}

} // namespace

McpJsonRestBridgeFilterConfig::McpJsonRestBridgeFilterConfig(
    const McpJsonRestBridgeProtoConfig& proto_config)
    : proto_config_(proto_config) {
  for (const auto& tool : proto_config.tool_config().tools()) {
    tool_to_http_rule_[tool.name()] = tool.http_rule();
  }
  ENVOY_LOG(debug, "Received MCP JSON REST Bridge config: {}", proto_config_.DebugString());
}

absl::StatusOr<HttpRule>
McpJsonRestBridgeFilterConfig::getHttpRule(absl::string_view tool_name) const {
  auto it = tool_to_http_rule_.find(tool_name);
  if (it == tool_to_http_rule_.end()) {
    return absl::InvalidArgumentError(
        fmt::format("Failed to find http rule for tool_name: {}", tool_name));
  }
  return it->second;
}

Http::FilterHeadersStatus
McpJsonRestBridgeFilter::decodeHeaders(Http::RequestHeaderMap& request_headers, bool) {
  mcp_operation_ = McpOperation::Undecided;

  if (request_headers.getMethodValue() != "POST") {
    ENVOY_LOG(warn, "Only POST method is supported for MCP. Received: {}",
              request_headers.getMethodValue());
    decoder_callbacks_->sendLocalReply(
        Http::Code::MethodNotAllowed, "Method Not Allowed",
        [](Http::ResponseHeaderMap& headers) {
          headers.addCopy(Http::LowerCaseString("allow"), "POST");
        },
        Grpc::Status::WellKnownGrpcStatus::InvalidArgument, "mcp_json_rest_bridge_filter_not_post");
    return Http::FilterHeadersStatus::Continue;
  }
  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus McpJsonRestBridgeFilter::decodeData(Buffer::Instance& data,
                                                           bool end_stream) {
  if (mcp_operation_ == McpOperation::Unspecified) {
    return Http::FilterDataStatus::Continue;
  }

  request_body_.move(data);

  if (!end_stream) {
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }

  std::string full_body = request_body_.toString();
  json request_body_json = json::parse(full_body, nullptr, false);

  if (request_body_json.is_discarded()) {
    ENVOY_LOG(error, "Failed to parse JSON-RPC request body.");
    sendErrorResponse(Http::Code::BadRequest,
                      "mcp_json_rest_bridge_filter_failed_to_parse_json_rpc_request",
                      generateErrorJsonResponse(-32700, "JSON parse error").dump());
    return Http::FilterDataStatus::Continue;
  }

  handleMcpMethod(request_body_json);

  return Http::FilterDataStatus::Continue;
}

Http::FilterHeadersStatus McpJsonRestBridgeFilter::encodeHeaders(Http::ResponseHeaderMap&, bool) {
  // TODO(guoyilin42): handle response headers.
  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus McpJsonRestBridgeFilter::encodeData(Buffer::Instance&, bool) {
  // TODO(guoyilin42): handle response data.
  return Http::FilterDataStatus::Continue;
}

Http::FilterTrailersStatus McpJsonRestBridgeFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  // TODO(guoyilin42): handle response trailers.
  return Http::FilterTrailersStatus::Continue;
}

void McpJsonRestBridgeFilter::handleMcpMethod(const nlohmann::json& json_rpc) {
  ENVOY_LOG(info, "Handling MCP JSON-RPC: {}", json_rpc.dump());
  if (!validateJsonRpcIdAndMethod(json_rpc).ok()) {
    return;
  }

  std::string method = json_rpc["method"];
  if (method == "tools/list") {
    mcp_operation_ = McpOperation::ToolsList;
    // TODO(guoyilin42): handle tools/list request.
  } else if (method == "initialize") {
    mcp_operation_ = McpOperation::Initialization;
    if (json_rpc.contains("params") && json_rpc["params"].contains("protocolVersion") &&
        json_rpc["params"]["protocolVersion"].is_string()) {
      decoder_callbacks_->sendLocalReply(
          Http::Code::OK, generateInitializeResponse(*session_id_).dump(), nullptr,
          Grpc::Status::WellKnownGrpcStatus::Ok, "mcp_json_rest_bridge_filter_initialize");
      return;
    }
    sendErrorResponse(
        Http::Code::BadRequest, "mcp_json_rest_bridge_filter_initialize_request_not_valid",
        generateErrorJsonResponse(-32602, "Missing valid protocolVersion in initialize "
                                          "request")
            .dump());
  } else if (method == "notifications/initialized") {
    mcp_operation_ = McpOperation::InitializationAck;
    decoder_callbacks_->sendLocalReply(
        Http::Code::Accepted, "",
        [](Http::ResponseHeaderMap& headers) {
          headers.addCopy(Http::LowerCaseString("content-length"), "0");
        },
        Grpc::Status::WellKnownGrpcStatus::Ok, "mcp_json_rest_bridge_filter_initialize_ack");
  } else if (method == "tools/call") {
    mcp_operation_ = McpOperation::ToolsCall;
    // TODO(guoyilin42): handle tools/call request.
  } else {
    sendErrorResponse(
        Http::Code::BadRequest, "mcp_json_rest_bridge_filter_method_not_supported",
        generateErrorJsonResponse(-32601, absl::StrCat("Method ", method, " is not supported"))
            .dump());
    return;
  }
}

void McpJsonRestBridgeFilter::sendErrorResponse(Http::Code response_code,
                                                absl::string_view response_code_details,
                                                absl::string_view response_body) {
  ENVOY_LOG(error, "Sending error response with response code details: {}", response_code_details);
  mcp_operation_ = McpOperation::OperationFailed;
  decoder_callbacks_->sendLocalReply(response_code, response_body, nullptr,
                                     Grpc::Status::WellKnownGrpcStatus::Internal,
                                     response_code_details);
}

absl::Status McpJsonRestBridgeFilter::validateJsonRpcIdAndMethod(const nlohmann::json& json_rpc) {
  absl::StatusOr<int> session_id = getSessionId(json_rpc);
  if (session_id.ok()) {
    session_id_ = *session_id;
  }
  if (!json_rpc.contains("method")) {
    sendErrorResponse(Http::Code::BadRequest, "mcp_json_rest_bridge_filter_method_not_found",
                      generateErrorJsonResponse(-32601, "Missing method field").dump());
    return absl::InvalidArgumentError("Missing method field");
  } else if (!json_rpc["method"].is_string()) {
    sendErrorResponse(Http::Code::BadRequest, "mcp_json_rest_bridge_filter_method_not_string",
                      generateErrorJsonResponse(-32601, "Method field is not a string").dump());
    return absl::InvalidArgumentError("Method field is not a string");
  } else if (json_rpc["method"] == "notifications/initialized") {
    // The notifications/initialized request is not required to have an ID
    // field.
  } else if (!session_id.ok()) {
    sendErrorResponse(Http::Code::BadRequest, "mcp_json_rest_bridge_filter_id_not_found",
                      generateErrorJsonResponse(-32600, "Missing ID field").dump());
    return absl::InvalidArgumentError("Missing ID field");
  }
  return absl::OkStatus();
}

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
