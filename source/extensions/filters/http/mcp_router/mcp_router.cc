#include "source/extensions/filters/http/mcp_router/mcp_router.h"

#include "source/common/common/fmt.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/json/json_streamer.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpRouter {

namespace {

constexpr absl::string_view kNameDelimiter = "__";
constexpr absl::string_view kSessionIdHeader = "mcp-session-id";

constexpr absl::string_view kGatewayName = "envoy-mcp-gateway";
constexpr absl::string_view kGatewayVersion = "1.0.0";
constexpr absl::string_view kProtocolVersion = "2025-06-18";

void copyRequestHeaders(const Http::RequestHeaderMap& source, Http::RequestHeaderMap& dest) {
  // Headers that we set explicitly or should not be forwarded
  static const absl::flat_hash_set<absl::string_view> kSkipHeaders = {
      ":method", ":path", ":authority", "host", "content-type", kSessionIdHeader};

  source.iterate([&dest](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    absl::string_view key = header.key().getStringView();

    if (!kSkipHeaders.contains(absl::AsciiStrToLower(key))) {
      dest.addCopy(Http::LowerCaseString(std::string(key)), header.value().getStringView());
    }
    return Http::HeaderMap::Iterate::Continue;
  });
}

} // namespace

McpMethod parseMethodString(absl::string_view method_str) {
  if (method_str == "initialize")
    return McpMethod::Initialize;
  if (method_str == "tools/list")
    return McpMethod::ToolsList;
  if (method_str == "tools/call")
    return McpMethod::ToolsCall;
  if (method_str == "ping")
    return McpMethod::Ping;
  if (method_str == "notifications/initialized")
    return McpMethod::NotificationInitialized;
  // TODO(botengyao): Add support for more MCP methods:
  // - resources/list, resources/read, resources/subscribe, resources/unsubscribe
  // - prompts/list, prompts/get
  // - completion/complete
  // - logging/setLevel
  // - notifications/* (other notifications)
  return McpMethod::Unknown;
}

BackendStreamCallbacks::BackendStreamCallbacks(const std::string& backend_name,
                                               std::function<void(BackendResponse)> on_complete)
    : backend_name_(backend_name), on_complete_(std::move(on_complete)) {
  response_.backend_name = backend_name;
}

void BackendStreamCallbacks::onHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  if (headers && headers->Status()) {
    response_.status_code = Http::Utility::getResponseStatus(*headers);
    response_.success = (response_.status_code >= 200 && response_.status_code < 300);

    // Extract session ID from response header
    auto session_header = headers->get(Http::LowerCaseString(std::string(kSessionIdHeader)));
    if (!session_header.empty()) {
      response_.session_id = std::string(session_header[0]->value().getStringView());
    }
  }

  if (end_stream) {
    complete();
  }
}

void BackendStreamCallbacks::onData(Buffer::Instance& data, bool end_stream) {
  response_.body.append(data.toString());
  if (end_stream) {
    complete();
  }
}

void BackendStreamCallbacks::onTrailers(Http::ResponseTrailerMapPtr&&) { complete(); }

void BackendStreamCallbacks::onComplete() { complete(); }

void BackendStreamCallbacks::onReset() {
  response_.success = false;
  response_.error = "Stream reset";
  complete();
}

void BackendStreamCallbacks::complete() {
  if (!completed_) {
    completed_ = true;
    ENVOY_LOG(debug, "Backend '{}' complete: status={}, body_size={}", backend_name_,
              response_.status_code, response_.body.size());
    on_complete_(std::move(response_));
  }
}

McpRouterFilter::McpRouterFilter(McpRouterConfigSharedPtr config)
    : config_(std::move(config)), muxdemux_(Http::MuxDemux::create(config_->factoryContext())) {}

McpRouterFilter::~McpRouterFilter() = default;

void McpRouterFilter::onDestroy() {
  if (multistream_) {
    multistream_.reset(); // This will reset all streams
  }
  stream_callbacks_.clear();
  upstream_headers_.clear();
}

Http::FilterHeadersStatus McpRouterFilter::decodeHeaders(Http::RequestHeaderMap& headers,
                                                         bool end_stream) {
  // TODO(botengyao): also supports /GET endpoints.
  if (headers.Method() &&
      headers.Method()->value().getStringView() == Http::Headers::get().MethodValues.Get) {
    sendHttpError(405, "Method Not Allowed");
    return Http::FilterHeadersStatus::StopIteration;
  }

  request_headers_ = &headers;

  // Extract session ID from header
  auto session_header = headers.get(Http::LowerCaseString(std::string(kSessionIdHeader)));
  if (!session_header.empty()) {
    encoded_session_id_ = std::string(session_header[0]->value().getStringView());
  }

  // TODO(botengyao): Extract subject from JWT filter metadata or authorization header.
  // For Initialize requests, there is no session yet, so we need to get the subject
  // from the authentication layer (e.g., JWT claims or auth header) and store it.
  // This subject will be used when building the composite session ID after backends respond.
  // Example: subject_ = extractSubjectFromJwtOrAuth(headers);

  if (end_stream) {
    // No body - invalid MCP POST request
    sendHttpError(400, "Missing request body");
    return Http::FilterHeadersStatus::StopIteration;
  }

  return Http::FilterHeadersStatus::StopIteration;
}

Http::FilterDataStatus McpRouterFilter::decodeData(Buffer::Instance& data, bool end_stream) {
  // Initialize on first data chunk - mcp_filter has already parsed metadata
  if (!initialized_) {
    if (!readMetadataFromMcpFilter()) {
      sendHttpError(400, "Invalid or missing MCP request");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    ENVOY_LOG(debug, "MCP router: method={}, request_id={}", static_cast<int>(method_),
              request_id_);

    if (!encoded_session_id_.empty() && !decodeAndParseSession()) {
      sendHttpError(400, "Invalid session ID");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    // Initialize connections based on method type
    switch (method_) {
    case McpMethod::Initialize:
      handleInitialize();
      break;

    case McpMethod::ToolsList:
      handleToolsList();
      break;

    case McpMethod::ToolsCall:
      handleToolsCall();
      break;

    case McpMethod::Ping:
      handlePing();
      return Http::FilterDataStatus::StopIterationNoBuffer;

    case McpMethod::NotificationInitialized:
      handleNotificationInitialized();
      break;

    default:
      sendHttpError(400, "Unsupported method");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    initialized_ = true;

    // Perform body rewriting if needed (e.g., tool name prefix stripping)
    // This is done once on the first data chunk after initialization.
    // Future methods like resources/get can also use this pattern.
    if (needs_body_rewrite_) {
      rewriteToolCallBody(data);
      needs_body_rewrite_ = false;
    }
  }

  streamData(data, end_stream);

  return Http::FilterDataStatus::StopIterationNoBuffer;
}

Http::FilterTrailersStatus McpRouterFilter::decodeTrailers(Http::RequestTrailerMap& trailers) {
  if (multistream_) {
    multistream_->multicastTrailers(trailers);
  }
  return Http::FilterTrailersStatus::Continue;
}

bool McpRouterFilter::readMetadataFromMcpFilter() {
  const auto& metadata = decoder_callbacks_->streamInfo().dynamicMetadata();

  // TODO(botengyao): make this filter_metadata configurable.
  auto filter_it = metadata.filter_metadata().find("mcp_proxy");
  if (filter_it == metadata.filter_metadata().end()) {
    return false;
  }

  const auto& mcp_metadata = filter_it->second;
  const auto& fields = mcp_metadata.fields();

  auto method_it = fields.find("method");
  if (method_it != fields.end() && method_it->second.has_string_value()) {
    method_ = parseMethodString(method_it->second.string_value());
  }

  if (method_ == McpMethod::Unknown) {
    ENVOY_LOG(warn, "unsupported or missing method in metadata");
    return false;
  }

  // Extract request ID.
  auto id_it = fields.find("id");
  if (id_it != fields.end() && id_it->second.has_number_value()) {
    request_id_ = static_cast<int64_t>(id_it->second.number_value());
  }

  if (method_ == McpMethod::ToolsCall) {
    const auto& fields = mcp_metadata.fields();
    auto params_it = fields.find("params");
    if (params_it != fields.end() && params_it->second.has_struct_value()) {
      const auto& params_fields = params_it->second.struct_value().fields();
      auto name_it = params_fields.find("name");
      if (name_it != params_fields.end() && name_it->second.has_string_value()) {
        tool_name_ = name_it->second.string_value();
      }
    }
  }

  return true;
}

bool McpRouterFilter::decodeAndParseSession() {
  std::string decoded = SessionCodec::decode(encoded_session_id_);
  if (decoded.empty()) {
    ENVOY_LOG(warn, "Failed to decode session ID");
    sendHttpError(400, "Invalid session ID");
    return false;
  }

  auto parsed = SessionCodec::parseCompositeSessionId(decoded);
  if (!parsed.ok()) {
    ENVOY_LOG(warn, "Failed to parse session: {}", parsed.status().message());
    sendHttpError(400, "Invalid session ID");
    return false;
  }

  route_name_ = parsed->route;
  session_subject_ = parsed->subject;
  backend_sessions_ = std::move(parsed->backend_sessions);

  return true;
}

std::pair<std::string, std::string> McpRouterFilter::parseToolName(const std::string& prefixed) {
  if (!config_->isMultiplexing()) {
    return {config_->defaultBackendName(), prefixed};
  }

  size_t pos = prefixed.find(kNameDelimiter);
  if (pos == std::string::npos) {
    if (!config_->defaultBackendName().empty()) {
      return {config_->defaultBackendName(), prefixed};
    }
    return {"", prefixed};
  }

  std::string backend = prefixed.substr(0, pos);
  std::string tool = prefixed.substr(pos + kNameDelimiter.size());

  if (config_->findBackend(backend) != nullptr) {
    return {backend, tool};
  }

  return {"", prefixed};
}

ssize_t McpRouterFilter::rewriteToolCallBody(Buffer::Instance& buffer) {
  if (tool_name_.empty() || tool_name_ == unprefixed_tool_name_) {
    return 0;
  }

  // Search for the prefixed tool name directly and replace with the unprefixed version.
  // This is simpler and handles any JSON formatting but less error proof.
  // TODO(botengyao): The mcp_filter's JSON decoder should provide a cursor/byte offset
  // for the params.name field, allowing precise replacement without pattern searching.
  ssize_t pos = buffer.search(tool_name_.data(), tool_name_.size(), 0);
  if (pos < 0) {
    return 0;
  }

  return rewriteAtPosition(buffer, pos, tool_name_, unprefixed_tool_name_);
}

ssize_t McpRouterFilter::rewriteAtPosition(Buffer::Instance& buffer, ssize_t pos,
                                           const std::string& search_str,
                                           const std::string& replacement) {
  ssize_t size_delta =
      static_cast<ssize_t>(replacement.size()) - static_cast<ssize_t>(search_str.size());

  Buffer::OwnedImpl new_buffer;

  if (pos > 0) {
    Buffer::OwnedImpl prefix;
    prefix.move(buffer, pos);
    new_buffer.move(prefix);
  }

  buffer.drain(search_str.size());
  new_buffer.add(replacement);
  new_buffer.move(buffer);
  buffer.move(new_buffer);

  return size_delta;
}

void McpRouterFilter::initializeFanout(AggregationCallback callback) {
  if (config_->backends().empty()) {
    sendHttpError(500, "No backends configured");
    return;
  }

  if (!muxdemux_->isIdle()) {
    ENVOY_LOG(warn, "MuxDemux not idle, cannot start new fanout");
    sendHttpError(500, "Internal error: concurrent fanout not allowed");
    return;
  }

  size_t expected = config_->backends().size();
  pending_responses_ = std::make_shared<std::vector<BackendResponse>>();
  pending_responses_->reserve(expected);
  response_count_ = std::make_shared<size_t>(0);
  aggregation_callback_ = std::move(callback);

  std::vector<Http::MuxDemux::Callbacks> mux_callbacks;
  mux_callbacks.reserve(expected);

  for (const auto& backend : config_->backends()) {
    auto responses = pending_responses_;
    auto count = response_count_;
    auto expected_count = expected;
    auto agg_callback = aggregation_callback_;

    auto stream_cb = std::make_shared<BackendStreamCallbacks>(
        backend.name, [responses, count, expected_count, agg_callback](BackendResponse resp) {
          responses->push_back(std::move(resp));
          if (++(*count) >= expected_count && agg_callback) {
            agg_callback(std::move(*responses));
          }
        });

    stream_callbacks_.push_back(stream_cb);
    mux_callbacks.push_back({
        .cluster_name = backend.cluster_name,
        .callbacks = std::weak_ptr<Http::AsyncClient::StreamCallbacks>(stream_cb),
    });
  }

  // TODO(botengyao): MuxDemux::multicast uses a single StreamOptions for all backends,
  // so per-backend timeouts are not currently supported. Using max timeout as a workaround.
  Http::AsyncClient::StreamOptions options;
  std::chrono::milliseconds max_timeout{0};
  for (const auto& backend : config_->backends()) {
    max_timeout = std::max(max_timeout, backend.timeout);
  }
  options.setTimeout(max_timeout);

  auto multistream_or = muxdemux_->multicast(options, mux_callbacks);
  if (!multistream_or.ok()) {
    ENVOY_LOG(error, "Failed to start multicast: {}", multistream_or.status().message());
    sendHttpError(500, "Failed to start fanout");
    return;
  }

  multistream_ = std::move(*multistream_or);

  upstream_headers_.clear();
  upstream_headers_.reserve(expected);

  // Store headers in upstream_headers_ because AsyncStreamImpl::sendHeaders only
  // stores a pointer to the headers.
  auto stream_it = multistream_->begin();
  for (const auto& backend : config_->backends()) {
    if (stream_it == multistream_->end())
      break;

    auto headers = createUpstreamHeaders(backend, backend_sessions_[backend.name]);
    upstream_headers_.push_back(std::move(headers));
    (*stream_it)->sendHeaders(*upstream_headers_.back(), false);
    ++stream_it;
  }
}

void McpRouterFilter::initializeSingleBackend(const McpBackendConfig& backend,
                                              std::function<void(BackendResponse)> callback) {
  if (!muxdemux_->isIdle()) {
    ENVOY_LOG(warn, "MuxDemux not idle, cannot start new single backend request for '{}'",
              backend.name);
    sendHttpError(500,
                  fmt::format("Internal error: concurrent request not allowed for backend '{}'",
                              backend.name));
    return;
  }

  single_backend_callback_ = std::move(callback);
  auto stream_cb = std::make_shared<BackendStreamCallbacks>(backend.name, single_backend_callback_);
  stream_callbacks_.push_back(stream_cb);

  Http::AsyncClient::StreamOptions options;
  options.setTimeout(backend.timeout);

  std::vector<Http::MuxDemux::Callbacks> mux_callbacks;
  mux_callbacks.push_back({
      .cluster_name = backend.cluster_name,
      .callbacks = std::weak_ptr<Http::AsyncClient::StreamCallbacks>(stream_cb),
  });

  auto multistream_or = muxdemux_->multicast(options, mux_callbacks);
  if (!multistream_or.ok()) {
    ENVOY_LOG(error, "Failed to start multicast for cluster '{}': {}", backend.cluster_name,
              multistream_or.status().message());
    sendHttpError(500, "Failed to start backend request");
    return;
  }

  multistream_ = std::move(*multistream_or);

  std::string backend_session;
  auto it = backend_sessions_.find(backend.name);
  if (it != backend_sessions_.end()) {
    backend_session = it->second;
  }

  // Store headers in upstream_headers_ because AsyncStreamImpl::sendHeaders only
  // stores a pointer to the headers.
  upstream_headers_.clear();
  upstream_headers_.reserve(1);
  auto headers = createUpstreamHeaders(backend, backend_session);
  upstream_headers_.push_back(std::move(headers));

  auto stream_it = multistream_->begin();
  if (stream_it != multistream_->end()) {
    (*stream_it)->sendHeaders(*upstream_headers_.back(), false);
  }
}

void McpRouterFilter::streamData(Buffer::Instance& data, bool end_stream) {
  if (multistream_) {
    multistream_->multicastData(data, end_stream);
  }
}

void McpRouterFilter::handleInitialize() {
  ENVOY_LOG(debug, "Initialize: setting up fanout to {} backends", config_->backends().size());

  initializeFanout([this](std::vector<BackendResponse> responses) {
    // TODO(botengyao): handle text/event-stream from backends.
    std::string response_body = aggregateInitialize(responses);

    absl::flat_hash_map<std::string, std::string> sessions;
    for (const auto& resp : responses) {
      if (resp.success && !resp.session_id.empty()) {
        sessions[resp.backend_name] = resp.session_id;
      }
    }

    if (sessions.empty()) {
      sendHttpError(500, "All backends failed to initialize");
      return;
    }

    // Build composite session ID
    // TODO(botengyao): extract subject from JWT filter metadata or authorization header.
    std::string composite = SessionCodec::buildCompositeSessionId(route_name_, "default", sessions);
    std::string encoded_session = SessionCodec::encode(composite);

    sendJsonResponse(response_body, encoded_session);
  });
}

void McpRouterFilter::handleToolsList() {
  ENVOY_LOG(debug, "tools/list: setting up fanout to {} backends", config_->backends().size());

  initializeFanout([this](std::vector<BackendResponse> responses) {
    std::string response_body = aggregateToolsList(responses);
    ENVOY_LOG(debug, "tools/list: response body: {}", response_body);
    sendJsonResponse(response_body, encoded_session_id_);
  });
}

void McpRouterFilter::handleToolsCall() {
  auto [backend_name, actual_tool] = parseToolName(tool_name_);

  if (backend_name.empty()) {
    sendHttpError(400, fmt::format("Invalid tool name '{}': cannot determine backend", tool_name_));
    return;
  }

  const McpBackendConfig* backend = config_->findBackend(backend_name);
  if (!backend) {
    sendHttpError(400, fmt::format("Unknown backend '{}' in tool name", backend_name));
    return;
  }

  // Store the unprefixed tool name for body rewriting
  unprefixed_tool_name_ = actual_tool;
  needs_body_rewrite_ = (tool_name_ != unprefixed_tool_name_);

  ENVOY_LOG(debug, "tools/call: backend='{}', tool='{}' -> '{}', needs_rewrite={}", backend_name,
            tool_name_, actual_tool, needs_body_rewrite_);

  initializeSingleBackend(*backend, [this](BackendResponse resp) {
    if (resp.success) {
      sendJsonResponse(resp.body, encoded_session_id_);
    } else {
      sendHttpError(500, resp.error.empty() ? "Backend request failed" : resp.error);
    }
  });
}

void McpRouterFilter::handlePing() {
  ENVOY_LOG(debug, "Ping: responding immediately with empty result");

  // Ping is a request/response pattern - respond immediately with empty result.
  // Per MCP spec: The receiver MUST respond promptly with an empty response.
  std::string response_body =
      absl::StrCat(R"({"jsonrpc":"2.0","id":)", request_id_, R"(,"result":{}})");
  sendJsonResponse(response_body, encoded_session_id_);
}

void McpRouterFilter::handleNotificationInitialized() {
  ENVOY_LOG(debug, "notifications/initialized: forwarding to {} backends",
            config_->backends().size());

  // Forward notifications/initialized to all backends and wait for responses.
  initializeFanout([this](std::vector<BackendResponse>) {
    // All backends have responded (or failed), send 202 to client.
    sendAccepted();
  });
}

std::string McpRouterFilter::aggregateInitialize(const std::vector<BackendResponse>& responses) {
  // Check if at least one backend succeeded.
  const bool any_success = std::any_of(responses.begin(), responses.end(),
                                       [](const BackendResponse& resp) { return resp.success; });

  if (!any_success) {
    return absl::StrCat(R"({"jsonrpc":"2.0","id":)", request_id_,
                        R"(,"error":{"code":-32603,"message":"All backends failed"}})");
  }

  // Return gateway capabilities.
  return absl::StrCat(
      R"({"jsonrpc":"2.0","id":)", request_id_, R"(,"result":{)", R"("protocolVersion":")",
      kProtocolVersion, R"(",)",
      R"("capabilities":{"tools":{"listChanged":true},"prompts":{"listChanged":true},"resources":{"listChanged":true,"subscribe":true}},)",
      R"("serverInfo":{"name":")", kGatewayName, R"(","version":")", kGatewayVersion, R"("},)",
      R"("instructions":"MCP gateway aggregating multiple backend servers.")", R"(}})");
}

std::string McpRouterFilter::aggregateToolsList(const std::vector<BackendResponse>& responses) {
  const bool is_multiplexing = config_->isMultiplexing();

  std::string output;
  Json::StringStreamer streamer(output);
  {
    auto root = streamer.makeRootMap();
    root->addKey("jsonrpc");
    root->addString("2.0");
    root->addKey("id");
    root->addNumber(request_id_);
    root->addKey("result");
    {
      auto result_map = root->addMap();
      result_map->addKey("tools");
      {
        auto tools_array = result_map->addArray();

        for (const auto& resp : responses) {
          if (!resp.success) {
            continue;
          }
          ENVOY_LOG(debug, "Aggregating tools list from backend '{}': {}", resp.backend_name,
                    resp.body);
          auto parsed_or = Json::Factory::loadFromString(resp.body);
          if (!parsed_or.ok()) {
            ENVOY_LOG(warn, "Failed to parse JSON from backend '{}': {}", resp.backend_name,
                      parsed_or.status().message());
            continue;
          }

          Json::ObjectSharedPtr parsed_body = *parsed_or;

          auto result_or = parsed_body->getObject("result");
          if (!result_or.ok() || !(*result_or)) {
            continue;
          }

          auto tools_or = (*result_or)->getObjectArray("tools");
          if (!tools_or.ok()) {
            continue;
          }

          for (const auto& tool : *tools_or) {
            if (!tool || !tool->isObject()) {
              continue;
            }

            auto name_or = tool->getString("name");
            if (!name_or.ok()) {
              continue;
            }

            auto tool_map = tools_array->addMap();
            tool_map->addKey("name");
            tool_map->addString(is_multiplexing
                                    ? absl::StrCat(resp.backend_name, kNameDelimiter, *name_or)
                                    : *name_or);

            auto desc_or = tool->getString("description", "");
            if (desc_or.ok() && !desc_or->empty()) {
              tool_map->addKey("description");
              tool_map->addString(*desc_or);
            }

            if (tool->hasObject("inputSchema")) {
              tool_map->addKey("inputSchema");
              auto schema_map = tool_map->addMap();
              schema_map->addKey("type");
              schema_map->addString("object");
            }
          }
        }
      }
    }
  }

  return output;
}

Http::RequestHeaderMapPtr
McpRouterFilter::createUpstreamHeaders(const McpBackendConfig& backend,
                                       const std::string& backend_session_id) {
  auto headers = Http::RequestHeaderMapImpl::create();

  // Set required headers for MCP backend
  headers->setMethod(Http::Headers::get().MethodValues.Post);
  headers->setPath(backend.path);
  // Use host_rewrite_literal if configured, otherwise pass through original host
  if (!backend.host_rewrite_literal.empty()) {
    headers->setHost(backend.host_rewrite_literal);
  } else if (request_headers_ != nullptr) {
    headers->setHost(request_headers_->getHostValue());
  }
  headers->setContentType("application/json");

  if (!backend_session_id.empty()) {
    headers->addCopy(Http::LowerCaseString(std::string(kSessionIdHeader)), backend_session_id);
  }

  // TODO(botengyao): Make header forwarding (authorization, etc.) configurable via proto config.
  if (request_headers_) {
    copyRequestHeaders(*request_headers_, *headers);

    // Adjust content-length when tool name rewriting changes body size.
    // Size delta is negative when removing the backend prefix from tool names.
    if (needs_body_rewrite_ && request_headers_->ContentLength()) {
      uint64_t original_length = 0;
      if (absl::SimpleAtoi(request_headers_->getContentLengthValue(), &original_length)) {
        // Use signed arithmetic: unprefixed is shorter, so delta is negative
        int64_t new_length = static_cast<int64_t>(original_length) +
                             static_cast<int64_t>(unprefixed_tool_name_.size()) -
                             static_cast<int64_t>(tool_name_.size());
        headers->setContentLength(new_length);
        ENVOY_LOG(debug, "Adjusted content-length: {} -> {}", original_length, new_length);
      }
    }
  }

  return headers;
}

void McpRouterFilter::sendJsonResponse(const std::string& body, const std::string& session_id) {
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->setContentType("application/json");
  headers->setContentLength(body.length());

  if (!session_id.empty()) {
    headers->addCopy(Http::LowerCaseString(std::string(kSessionIdHeader)), session_id);
  }

  decoder_callbacks_->encodeHeaders(std::move(headers), body.empty(), "mcp_router");
  ENVOY_LOG(debug, "Sending JSON response: {}", body);
  if (!body.empty()) {
    Buffer::OwnedImpl response_body(body);
    decoder_callbacks_->encodeData(response_body, true);
  }
}

void McpRouterFilter::sendAccepted() {
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(202);

  if (!encoded_session_id_.empty()) {
    headers->addCopy(Http::LowerCaseString(std::string(kSessionIdHeader)), encoded_session_id_);
  }

  decoder_callbacks_->encodeHeaders(std::move(headers), true, "mcp_router");
}

void McpRouterFilter::sendHttpError(uint64_t status_code, const std::string& message) {
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(status_code);
  headers->setContentType("text/plain");
  headers->setContentLength(message.length());

  decoder_callbacks_->encodeHeaders(std::move(headers), message.empty(), "mcp_router");

  if (!message.empty()) {
    Buffer::OwnedImpl body(message);
    decoder_callbacks_->encodeData(body, true);
  }
}

} // namespace McpRouter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
