#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {

enum class BridgeStatus {
  Ok,
  HttpRequestMethodNotPost,
  RequestTooLarge,
  RequestFailedToParseJsonRpc,
  RequestUnsupportedMcpVersion,
  RequestInitializeNotValid,
  RequestMcpMethodNotSupported,
  RequestMcpMethodMalformed,
  RequestIdNotFound,
  RequestToolsCallToolNameMissing,
  RequestToolsCallToolNameUnknown,
  RequestToolsCallArgumentsMalformed,
  RequestToolsCallMissingRequiredArg,
  RequestToolsCallPathTraversalRejected,
  InternalToolsCallInvalidHttpRule,
  InternalToolsListPassthrough,
  ResponseTooLarge,
  ResponseToolsCallInvalidUtf8,
  ResponseHttpStatusError,
  ResponseFailedToParseJsonRpc,
};

absl::string_view bridgeStatusToString(BridgeStatus status);

namespace BridgeStatusValues {
inline constexpr absl::string_view STATUS = "status";
inline constexpr absl::string_view OK = "mcp_json_rest_bridge_ok";
inline constexpr absl::string_view HTTP_REQUEST_METHOD_NOT_POST =
    "mcp_json_rest_bridge_http_request_method_not_post";
inline constexpr absl::string_view REQUEST_TOO_LARGE = "mcp_json_rest_bridge_request_too_large";
inline constexpr absl::string_view REQUEST_FAILED_TO_PARSE_JSON_RPC =
    "mcp_json_rest_bridge_request_failed_to_parse_json_rpc";
inline constexpr absl::string_view REQUEST_UNSUPPORTED_MCP_VERSION =
    "mcp_json_rest_bridge_request_unsupported_mcp_version";
inline constexpr absl::string_view REQUEST_INITIALIZE_NOT_VALID =
    "mcp_json_rest_bridge_request_initialize_not_valid";
inline constexpr absl::string_view REQUEST_MCP_METHOD_NOT_SUPPORTED =
    "mcp_json_rest_bridge_request_mcp_method_not_supported";
inline constexpr absl::string_view REQUEST_MCP_METHOD_MALFORMED =
    "mcp_json_rest_bridge_request_mcp_method_malformed";
inline constexpr absl::string_view REQUEST_ID_NOT_FOUND =
    "mcp_json_rest_bridge_request_id_not_found";
inline constexpr absl::string_view REQUEST_TOOLS_CALL_TOOL_NAME_MISSING =
    "mcp_json_rest_bridge_request_tools_call_tool_name_missing";
inline constexpr absl::string_view REQUEST_TOOLS_CALL_TOOL_NAME_UNKNOWN =
    "mcp_json_rest_bridge_request_tools_call_tool_name_unknown";
inline constexpr absl::string_view REQUEST_TOOLS_CALL_ARGUMENTS_MALFORMED =
    "mcp_json_rest_bridge_request_tools_call_arguments_malformed";
inline constexpr absl::string_view REQUEST_TOOLS_CALL_MISSING_REQUIRED_ARG =
    "mcp_json_rest_bridge_request_tools_call_missing_required_arg";
inline constexpr absl::string_view REQUEST_TOOLS_CALL_PATH_TRAVERSAL_REJECTED =
    "mcp_json_rest_bridge_request_tools_call_path_traversal_rejected";
inline constexpr absl::string_view INTERNAL_TOOLS_CALL_INVALID_HTTP_RULE =
    "mcp_json_rest_bridge_internal_tools_call_invalid_http_rule";
inline constexpr absl::string_view INTERNAL_TOOLS_LIST_PASSTHROUGH =
    "mcp_json_rest_bridge_internal_tools_list_passthrough";
inline constexpr absl::string_view RESPONSE_TOO_LARGE = "mcp_json_rest_bridge_response_too_large";
inline constexpr absl::string_view RESPONSE_TOOLS_CALL_INVALID_UTF8 =
    "mcp_json_rest_bridge_response_tools_call_invalid_utf8";
inline constexpr absl::string_view RESPONSE_HTTP_STATUS_ERROR =
    "mcp_json_rest_bridge_response_http_status_error";
inline constexpr absl::string_view RESPONSE_FAILED_TO_PARSE_JSON_RPC =
    "mcp_json_rest_bridge_response_failed_to_parse_json_rpc";
} // namespace BridgeStatusValues

} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
