#include "source/extensions/filters/http/mcp_proxy/mcp_filter.h"
#include "source/common/config/utility.h"
#include "source/common/http/utility.h"
#include "source/common/common/logger.h"
#include "source/common/http/headers.h"
#include "absl/status/status.h"
#include <nlohmann/json.hpp>
namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpProxy {
namespace FieldName {
constexpr absl::string_view RequestMethod = "method";
constexpr absl::string_view RequestJsonRpcVersion = "jsonrpc";
constexpr absl::string_view RequestId = "id";
constexpr absl::string_view RequestContentType = "application/json";
constexpr absl::string_view RequestParams = "params";
constexpr absl::string_view ResponseId = "id";
constexpr absl::string_view ResponseError = "error";
constexpr absl::string_view ResponseToolsError = "isError";
constexpr absl::string_view ResponseResult = "result";
constexpr absl::string_view ResponseContent = "content";
constexpr absl::string_view ResponseResultType = "type";
constexpr absl::string_view ResponseResultValue = "text";
constexpr absl::string_view ResponseJsonRpcVersion = "jsonrpc";
constexpr absl::string_view ResponseProtocolVersion = "protocolVersion";
constexpr absl::string_view ResponseCapabilities = "capabilities";
constexpr absl::string_view ResponseTools = "tools";
constexpr absl::string_view ResponseServerInfo = "serverInfo";
constexpr absl::string_view ResponseInstructions = "instructions";
constexpr absl::string_view ResponseToolsListChanged = "listChanged";
constexpr absl::string_view ResponseServerName = "name";
constexpr absl::string_view ResponseServerTitle = "title";
constexpr absl::string_view ResponseServerVersion = "version";
constexpr absl::string_view ErrorCode = "code";
constexpr absl::string_view ErrorMessage = "message";
constexpr absl::string_view ResponseStructuredContent = "structuredContent";
} // namespace FieldName
namespace FieldValue {
constexpr absl::string_view ResponseResultType = "text";
} // namespace FieldValue
constexpr absl::string_view RequestMethodToolsList = "tools/list";
constexpr absl::string_view RequestMethodToolsCall = "tools/call";
constexpr absl::string_view RequestMethodInitialize = "initialize";
constexpr absl::string_view RequestMethodPing = "ping";
constexpr absl::string_view RequestMethodNotificationsInitialized = "notifications/initialized";
constexpr absl::string_view JsonRpcVersion = "2.0";
constexpr absl::string_view McpSessionId = "mcp-session-id";
constexpr absl::string_view McpProtocolVersion = "2025-06-18";
constexpr int ErrorCodeParseError = -32700;
constexpr int ErrorCodeInvalidRequest = -32600;
constexpr int ErrorCodeMethodNotFound = -32601;
constexpr int ErrorCodeInvalidParams = -32602;
constexpr int ErrorCodeInternalError = -32603;
namespace {
class EmptyMcpHandlerFactory : public Envoy::Http::McpHandlerFactory {
public:
  Envoy::Http::McpHandlerPtr create() const override { return nullptr; }
};
} // namespace
McpFilterConfig::McpFilterConfig(const ProtoConfig& config,
                                 Server::Configuration::CommonFactoryContext& context) {
  if (!config.has_mcp_handler()) {
    factory_ = std::make_shared<EmptyMcpHandlerFactory>();
    return;
  }
  server_name_ = config.server_name();
  server_title_ = config.server_title();
  server_version_ = config.server_version();
  server_instructions_ = config.server_instructions();
  structured_content_ = config.structured_content();
  auto& factory =
      Envoy::Config::Utility::getAndCheckFactoryByName<Envoy::Http::McpHandlerFactoryConfig>(
          config.mcp_handler().name());
  auto typed_config = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.mcp_handler().typed_config(), context.messageValidationVisitor(), factory);
  factory_ = factory.createMcpHandlerFactory(*typed_config, context);
}
void McpFilter::initPerRouteConfig() {
  const auto route_local =
      Http::Utility::resolveMostSpecificPerFilterConfig<McpFilterConfig>(decoder_callbacks_);
  if (route_local != nullptr) {
    ENVOY_LOG(debug, "McpProxyFilter: initPerRouteConfig: use route_local");
  }
  per_route_config_ =
      route_local != nullptr ? std::make_shared<McpFilterConfig>(*route_local) : config_;
}
Http::FilterHeadersStatus McpFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                   bool end_stream) {
  initPerRouteConfig();
  mcp_handler_ = per_route_config_->createMcpHandler();
  if (mcp_handler_ == nullptr) {
    ENVOY_LOG(debug, "McpProxyFilter: decodeHeaders: mcp_handler is nullptr, pass through");
    return Http::FilterHeadersStatus::Continue;
  }
  request_headers_ = &headers;
  if (end_stream) {
    // TODO: handle only headers case
    return Http::FilterHeadersStatus::Continue;
  }
  return Http::FilterHeadersStatus::StopIteration;
}
Http::FilterDataStatus McpFilter::decodeData(Buffer::Instance& data, bool) {
  if (mcp_handler_ == nullptr) {
    ENVOY_LOG(debug, "McpProxyFilter: decodeData: mcp_handler is nullptr, pass through");
    return Http::FilterDataStatus::Continue;
  }
  if (request_headers_->getContentTypeValue().find(FieldName::RequestContentType) ==
      std::string::npos) {
    ENVOY_LOG(error,
              "McpProxyFilter: decodeData: content type is not application/json, pass through");
    return Http::FilterDataStatus::Continue;
  }
  absl::string_view body(static_cast<const char*>(data.linearize(data.length())), data.length());
  ENVOY_LOG(debug, "McpProxyFilter: decodeData: {}", body);
  // parse data to json object
  nlohmann::json req_json;
  try {
    req_json = nlohmann::json::parse(body);
    ENVOY_LOG(debug, "McpProxyFilter: decodeData: parsed json: {}", req_json.dump());
  } catch (const std::exception& e) {
    ENVOY_LOG(error, "McpProxyFilter: decodeData: json parse error: {}", e.what());
    sendProtocolError(nlohmann::json{
        {FieldName::ErrorCode, ErrorCodeParseError},
        {FieldName::ErrorMessage, std::string("Failed to parse JSON request: ") + e.what()}});
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  if (!req_json.contains(FieldName::RequestJsonRpcVersion) ||
      req_json[FieldName::RequestJsonRpcVersion] != JsonRpcVersion) {
    ENVOY_LOG(error, "McpProxyFilter: decodeData: jsonrpc not found or version not 2.0");
    sendProtocolError(
        nlohmann::json{{FieldName::ErrorCode, ErrorCodeInvalidRequest},
                       {FieldName::ErrorMessage, std::string("Invalid JSON-RPC request")}});
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  if (!req_json.contains(FieldName::RequestMethod)) {
    ENVOY_LOG(error, "McpProxyFilter: decodeData: jsonrpc contains no method");
    sendProtocolError(
        nlohmann::json{{FieldName::ErrorCode, ErrorCodeInvalidRequest},
                       {FieldName::ErrorMessage, std::string("Invalid JSON-RPC request")}});
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  std::string method = req_json[FieldName::RequestMethod];
  if (!req_json.contains(FieldName::RequestId) || req_json[FieldName::RequestId].is_null()) {
    request_id_ = absl::nullopt;
  } else if (req_json[FieldName::RequestId].is_number()) {
    request_id_ = req_json[FieldName::RequestId].get<int64_t>();
  } else if (req_json[FieldName::RequestId].is_string()) {
    request_id_ = std::stoll(req_json[FieldName::RequestId].get<std::string>());
  } else {
    request_id_ = absl::nullopt;
  }
  if (method == RequestMethodInitialize) {
    ENVOY_LOG(debug, "McpProxyFilter: decodeData: initialize method");
    nlohmann::json resp_json;
    resp_json[FieldName::ResponseJsonRpcVersion] = JsonRpcVersion;
    if (request_id_.has_value()) {
      resp_json[FieldName::ResponseId] = request_id_.value();
    }
    nlohmann::json result;
    result[FieldName::ResponseProtocolVersion] = McpProtocolVersion;
    nlohmann::json capabilities;
    nlohmann::json tools;
    tools[FieldName::ResponseToolsListChanged] = false;
    capabilities[FieldName::ResponseTools] = tools;
    result[FieldName::ResponseCapabilities] = capabilities;
    nlohmann::json serverInfo;
    serverInfo[FieldName::ResponseServerName] = per_route_config_->getServerName();
    serverInfo[FieldName::ResponseServerTitle] = per_route_config_->getServerTitle();
    serverInfo[FieldName::ResponseServerVersion] = per_route_config_->getServerVersion();
    result[FieldName::ResponseServerInfo] = serverInfo;
    result[FieldName::ResponseInstructions] = per_route_config_->getServerInstructions();
    resp_json[FieldName::ResponseResult] = result;
    std::string resp_str = resp_json.dump();
    auto modify_headers = [](Envoy::Http::ResponseHeaderMap& headers) {
      headers.setReferenceContentType(Envoy::Http::Headers::get().ContentTypeValues.Json);
    };
    local_reply_sent_ = true;
    decoder_callbacks_->sendLocalReply(Http::Code::OK, resp_str, modify_headers, absl::nullopt,
                                       "mcp_initialize_reply");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  if (method == RequestMethodToolsList) {
    nlohmann::json resp_json;
    resp_json[FieldName::ResponseJsonRpcVersion] = JsonRpcVersion;
    if (request_id_.has_value()) {
      resp_json[FieldName::ResponseId] = request_id_.value();
    }
    nlohmann::json result;
    absl::Status status = mcp_handler_->getToolsListJson(result);
    if (!status.ok()) {
      ENVOY_LOG(error, "McpProxyFilter: decodeData: getToolsListJson error: {}", status.message());
      sendProtocolError(nlohmann::json{
          {FieldName::ErrorCode, ErrorCodeInternalError},
          {FieldName::ErrorMessage, std::string("Internal error: ") + status.message().data()}});
    }
    resp_json[FieldName::ResponseResult] = result;
    std::string resp_str = resp_json.dump();
    auto modify_headers = [&resp_str](Envoy::Http::ResponseHeaderMap& headers) {
      headers.setReferenceContentType(Envoy::Http::Headers::get().ContentTypeValues.Json);
      headers.setContentLength(resp_str.length());
    };
    local_reply_sent_ = true;
    decoder_callbacks_->sendLocalReply(Http::Code::OK, resp_str, modify_headers, absl::nullopt,
                                       "mcp_tools_list_reply");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  if (method == RequestMethodNotificationsInitialized) {
    ENVOY_LOG(debug, "McpProxyFilter: decodeData: notifications/initialized method");
    decoder_callbacks_->sendLocalReply(Http::Code::Accepted, "", nullptr, absl::nullopt,
                                       "mcp_notifications_initialized_reply");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  if (method == RequestMethodPing) {
    ENVOY_LOG(debug, "McpProxyFilter: decodeData: ping method");
    nlohmann::json resp_json;
    resp_json[FieldName::ResponseJsonRpcVersion] = JsonRpcVersion;
    if (request_id_.has_value()) {
      resp_json[FieldName::ResponseId] = request_id_.value();
    }
    resp_json[FieldName::ResponseResult] = nlohmann::json::object();
    std::string resp_str = resp_json.dump();
    auto modify_headers = [&resp_str](Envoy::Http::ResponseHeaderMap& headers) {
      headers.setReferenceContentType(Envoy::Http::Headers::get().ContentTypeValues.Json);
      headers.setContentLength(resp_str.length());
    };
    local_reply_sent_ = true;
    decoder_callbacks_->sendLocalReply(Http::Code::OK, resp_str, modify_headers, absl::nullopt,
                                       "mcp_ping_reply");
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  if (method == RequestMethodToolsCall) {
    Envoy::Http::McpHandler::McpRequest mcp_req;
    mcp_req.jsonrpc = JsonRpcVersion;
    if (request_id_.has_value()) {
      mcp_req.id = request_id_.value();
    }
    mcp_req.method = method;
    mcp_req.params = req_json.contains(FieldName::RequestParams)
                         ? req_json[FieldName::RequestParams]
                         : nlohmann::json();
    const auto result = request_headers_->get(Http::LowerCaseString(McpSessionId));
    if (!result.empty()) {
      std::string session_id(result[0]->value().getStringView());
      mcp_req.mcp_session_id = session_id;
    }
    absl::Status status = mcp_handler_->handleMcpToolsCall(mcp_req, *request_headers_, data);
    if (!status.ok()) {
      ENVOY_LOG(error, "McpProxyFilter: decodeData: handleMcpToolsCall error: {}",
                status.message());
      if (status.code() == absl::StatusCode::kInvalidArgument) {
        sendProtocolError(nlohmann::json{{FieldName::ErrorCode, ErrorCodeInvalidRequest},
                                         {FieldName::ErrorMessage, status.message().data()}});
      } else if (status.code() == absl::StatusCode::kNotFound) {
        sendProtocolError(nlohmann::json{{FieldName::ErrorCode, ErrorCodeInvalidParams},
                                         {FieldName::ErrorMessage, status.message().data()}});
      } else {
        sendProtocolError(nlohmann::json{
            {FieldName::ErrorCode, ErrorCodeInternalError},
            {FieldName::ErrorMessage, std::string("Internal error: ") + status.message().data()}});
      }
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }
    ENVOY_LOG(debug, "McpProxyFilter: decodeData: handleMcpToolsCall success,{}", data.toString());
    return Http::FilterDataStatus::Continue;
  } else {
    ENVOY_LOG(error, "McpProxyFilter: decodeData: method not found: {}",
              req_json[FieldName::RequestMethod]);
    sendProtocolError(nlohmann::json{{FieldName::ErrorCode, ErrorCodeMethodNotFound},
                                     {FieldName::ErrorMessage, std::string("Method not found")}});
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  return Http::FilterDataStatus::Continue;
}
Http::FilterHeadersStatus McpFilter::encodeHeaders(Http::ResponseHeaderMap& headers,
                                                   bool end_stream) {
  if (mcp_handler_ == nullptr) {
    ENVOY_LOG(debug, "McpProxyFilter: encodeHeaders: mcp_handler is nullptr, pass through");
    return Http::FilterHeadersStatus::Continue;
  }
  response_headers_ = &headers;
  if (end_stream) {
    // TODO: handle only headers case
    return Http::FilterHeadersStatus::Continue;
  }
  return Http::FilterHeadersStatus::StopIteration;
}
Http::FilterDataStatus McpFilter::encodeData(Buffer::Instance& data, bool) {
  if (mcp_handler_ == nullptr) {
    ENVOY_LOG(debug, "McpProxyFilter: encodeData: mcp_handler is nullptr, pass through");
    return Http::FilterDataStatus::Continue;
  }
  ENVOY_LOG(debug, "McpProxyFilter: encodeData: data: {}", data.toString());
  if (local_reply_sent_) {
    local_reply_sent_ = false;
    ENVOY_LOG(debug, "McpProxyFilter: encodeData: local_reply_sent_, pass through");
    return Http::FilterDataStatus::Continue;
  }
  Envoy::Http::McpHandler::McpResponse mcp_resp;
  absl::Status status = mcp_handler_->buildMcpResponse(*response_headers_, data, mcp_resp);
  if (!status.ok()) {
    ENVOY_LOG(debug, "McpProxyFilter: encodeData: buildMcpResponse error: {}", status.message());
    sendProtocolError(nlohmann::json{
        {FieldName::ErrorCode, ErrorCodeInternalError},
        {FieldName::ErrorMessage, std::string("Internal error: ") + status.message().data()}});
    return Http::FilterDataStatus::StopIterationNoBuffer;
  }
  nlohmann::json resp_json;
  resp_json[FieldName::ResponseJsonRpcVersion] = JsonRpcVersion;
  if (request_id_.has_value()) {
    resp_json[FieldName::ResponseId] = request_id_.value();
  }
  if (!mcp_resp.error.empty()) {
    nlohmann::json content_item;
    content_item[FieldName::ResponseResultType] = FieldValue::ResponseResultType;
    content_item[FieldName::ResponseResultValue] = mcp_resp.error;
    resp_json[FieldName::ResponseResult][FieldName::ResponseContent].push_back(content_item);
    resp_json[FieldName::ResponseResult][FieldName::ResponseToolsError] = true;
    ENVOY_LOG(debug, "McpProxyFilter: encodeData: error: {}", mcp_resp.error);
  } else if (!mcp_resp.result.is_null() && !mcp_resp.result.empty()) {
    if (per_route_config_->getStructuredContent()) {
      resp_json[FieldName::ResponseResult][FieldName::ResponseStructuredContent] = mcp_resp.result;
    }
    resp_json[FieldName::ResponseResult][FieldName::ResponseContent] = nlohmann::json::array();
    nlohmann::json content_item;
    content_item[FieldName::ResponseResultType] = FieldValue::ResponseResultType;
    content_item[FieldName::ResponseResultValue] = mcp_resp.result.dump();
    resp_json[FieldName::ResponseResult][FieldName::ResponseContent].push_back(content_item);
    ENVOY_LOG(debug, "McpProxyFilter: encodeData: result: {}", mcp_resp.result.dump());
  } else {
    ENVOY_LOG(debug, "McpProxyFilter: encodeData: no result or error");
  }
  std::string resp_str = resp_json.dump();
  data.drain(data.length());
  data.add(resp_str);
  response_headers_->setContentLength(resp_str.length());
  response_headers_->setReferenceContentType(Envoy::Http::Headers::get().ContentTypeValues.Json);
  ENVOY_LOG(debug, "McpProxyFilter: buildJsonRpcResponse: resp_str: {}", resp_str);
  return Http::FilterDataStatus::Continue;
}
Http::FilterTrailersStatus McpFilter::decodeTrailers(Http::RequestTrailerMap&) {
  return Http::FilterTrailersStatus::StopIteration;
}
Http::FilterTrailersStatus McpFilter::encodeTrailers(Http::ResponseTrailerMap&) {
  return Http::FilterTrailersStatus::StopIteration;
}
Http::Filter1xxHeadersStatus McpFilter::encode1xxHeaders(Http::ResponseHeaderMap&) {
  return Http::Filter1xxHeadersStatus::Continue;
}
Http::FilterMetadataStatus McpFilter::encodeMetadata(Http::MetadataMap&) {
  return Http::FilterMetadataStatus::Continue;
}
void McpFilter::onDestroy() {
  // TODO: implement
}
void McpFilter::sendProtocolError(nlohmann::json error_msg) {
  ENVOY_LOG(error, "McpProxyFilter: sendProtocolError: {}", error_msg.dump());
  nlohmann::json resp_json;
  resp_json[FieldName::ResponseJsonRpcVersion] = JsonRpcVersion;
  if (request_id_.has_value()) {
    resp_json[FieldName::ResponseId] = request_id_.value();
  }
  resp_json[FieldName::ResponseError] = error_msg;
  std::string resp_str = resp_json.dump();
  auto modify_headers = [&resp_str](Envoy::Http::ResponseHeaderMap& headers) {
    headers.setReferenceContentType(Envoy::Http::Headers::get().ContentTypeValues.Json);
    headers.setContentLength(resp_str.length());
  };
  local_reply_sent_ = true;
  decoder_callbacks_->sendLocalReply(Http::Code::OK, resp_str, modify_headers, absl::nullopt,
                                     "mcp_jsonrpc_protocol_error_reply");
}
} // namespace McpProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
