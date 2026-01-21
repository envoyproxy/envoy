#include "source/extensions/filters/http/mcp_router/mcp_router.h"

#include "source/common/common/fmt.h"
#include "source/common/common/macros.h"
#include "source/common/config/metadata.h"
#include "source/common/http/headers.h"
#include "source/common/http/message_impl.h"
#include "source/common/http/utility.h"
#include "source/common/json/json_loader.h"
#include "source/common/json/json_streamer.h"
#include "source/common/protobuf/utility.h"

#include "absl/container/flat_hash_map.h"
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

// Static map for MCP method string lookup.
using McpMethodMap = absl::flat_hash_map<absl::string_view, McpMethod>;

const McpMethodMap& mcpMethodMap() {
  // TODO(botengyao): Add support for more MCP methods:
  // - completion/complete
  // - logging/setLevel
  // - notifications/* (other notifications)
  CONSTRUCT_ON_FIRST_USE(McpMethodMap,
                         {{"initialize", McpMethod::Initialize},
                          {"tools/list", McpMethod::ToolsList},
                          {"tools/call", McpMethod::ToolsCall},
                          {"resources/list", McpMethod::ResourcesList},
                          {"resources/read", McpMethod::ResourcesRead},
                          {"resources/subscribe", McpMethod::ResourcesSubscribe},
                          {"resources/unsubscribe", McpMethod::ResourcesUnsubscribe},
                          {"prompts/list", McpMethod::PromptsList},
                          {"prompts/get", McpMethod::PromptsGet},
                          {"ping", McpMethod::Ping},
                          {"notifications/initialized", McpMethod::NotificationInitialized}});
}

McpMethod parseMethodString(absl::string_view method_str) {
  auto it = mcpMethodMap().find(method_str);
  return it != mcpMethodMap().end() ? it->second : McpMethod::Unknown;
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
      // decodeAndParseSession already sent the appropriate error response
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    // Initialize connections based on method type.
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

    case McpMethod::ResourcesList:
      handleResourcesList();
      break;

    case McpMethod::ResourcesRead:
      handleResourcesRead();
      break;

    case McpMethod::ResourcesSubscribe:
      handleResourcesSubscribe();
      break;

    case McpMethod::ResourcesUnsubscribe:
      handleResourcesUnsubscribe();
      break;

    case McpMethod::PromptsList:
      handlePromptsList();
      break;

    case McpMethod::PromptsGet:
      handlePromptsGet();
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

    // Perform body rewriting if needed (e.g., tool/prompt name or URI prefix stripping).
    // This is done once on the first data chunk after initialization.
    if (needs_body_rewrite_) {
      if (method_ == McpMethod::ToolsCall) {
        rewriteToolCallBody(data);
      } else if (method_ == McpMethod::ResourcesRead || method_ == McpMethod::ResourcesSubscribe ||
                 method_ == McpMethod::ResourcesUnsubscribe) {
        rewriteResourceUriBody(data);
      } else if (method_ == McpMethod::PromptsGet) {
        rewritePromptsGetBody(data);
      }
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

  // Extract method-specific parameters from metadata.
  auto params_it = fields.find("params");
  if (params_it != fields.end() && params_it->second.has_struct_value()) {
    const auto& params_fields = params_it->second.struct_value().fields();

    if (method_ == McpMethod::ToolsCall) {
      auto name_it = params_fields.find("name");
      if (name_it != params_fields.end() && name_it->second.has_string_value()) {
        tool_name_ = name_it->second.string_value();
      }
    } else if (method_ == McpMethod::ResourcesRead || method_ == McpMethod::ResourcesSubscribe ||
               method_ == McpMethod::ResourcesUnsubscribe) {
      auto uri_it = params_fields.find("uri");
      if (uri_it != params_fields.end() && uri_it->second.has_string_value()) {
        resource_uri_ = uri_it->second.string_value();
      }
    } else if (method_ == McpMethod::PromptsGet) {
      auto name_it = params_fields.find("name");
      if (name_it != params_fields.end() && name_it->second.has_string_value()) {
        prompt_name_ = name_it->second.string_value();
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

  if (!validateSubjectIfRequired()) {
    return false;
  }

  return true;
}

absl::StatusOr<std::string> McpRouterFilter::getAuthenticatedSubject() {
  const auto& source = config_->subjectSource();

  if (absl::holds_alternative<HeaderSubjectSource>(source)) {
    const auto& header_source = absl::get<HeaderSubjectSource>(source);
    auto header = request_headers_->get(Http::LowerCaseString(header_source.header_name));
    if (header.empty()) {
      return absl::NotFoundError(
          absl::StrCat("Header '", header_source.header_name, "' not found"));
    }
    return std::string(header[0]->value().getStringView());
  }

  if (absl::holds_alternative<MetadataSubjectSource>(source)) {
    const auto& metadata_source = absl::get<MetadataSubjectSource>(source);
    const auto& metadata = decoder_callbacks_->streamInfo().dynamicMetadata();

    const auto& value = Config::Metadata::metadataValue(&metadata, metadata_source.filter,
                                                        metadata_source.path_keys);

    if (value.kind_case() == Protobuf::Value::KIND_NOT_SET) {
      return absl::NotFoundError("Subject not found in metadata path");
    }

    if (!value.has_string_value()) {
      return absl::InvalidArgumentError("Subject metadata value is not a string");
    }

    return value.string_value();
  }

  return absl::InvalidArgumentError("No subject source configured");
}

bool McpRouterFilter::validateSubjectIfRequired() {
  // Only validate if enforcement is enabled.
  if (!config_->shouldEnforceValidation()) {
    return true;
  }

  auto auth_subject = getAuthenticatedSubject();
  if (!auth_subject.ok()) {
    ENVOY_LOG(warn, "Failed to get authenticated subject: {}", auth_subject.status().message());
    sendHttpError(403, "Unable to verify session identity");
    return false;
  }

  if (session_subject_ != *auth_subject) {
    ENVOY_LOG(warn, "Session subject mismatch: session='{}'", session_subject_);
    sendHttpError(403, "Session identity mismatch");
    return false;
  }

  ENVOY_LOG(debug, "Subject validation passed for '{}'", session_subject_);
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

std::pair<std::string, std::string> McpRouterFilter::parseResourceUri(const std::string& uri) {
  // Resource URIs use the format: <backend>+<scheme>://<path>
  // Example: "time+file://current" -> backend="time", rewritten_uri="file://current"
  // This avoids conflicts where backend names might match scheme names.
  if (!config_->isMultiplexing()) {
    return {config_->defaultBackendName(), uri};
  }

  // Find the scheme separator "://"
  size_t scheme_sep = uri.find("://");
  if (scheme_sep == std::string::npos) {
    // No scheme, use default backend if available.
    if (!config_->defaultBackendName().empty()) {
      return {config_->defaultBackendName(), uri};
    }
    return {"", uri};
  }

  // Look for the backend+scheme delimiter ('+') before "://"
  std::string prefix = uri.substr(0, scheme_sep);
  size_t plus_pos = prefix.find('+');

  if (plus_pos != std::string::npos) {
    // Format: backend+scheme://path
    std::string backend = prefix.substr(0, plus_pos);
    std::string scheme = prefix.substr(plus_pos + 1);
    std::string path = uri.substr(scheme_sep + 3); // Skip "://"

    if (config_->findBackend(backend) != nullptr) {
      // Rewrite URI with the original scheme for the backend.
      return {backend, absl::StrCat(scheme, "://", path)};
    }
  }

  // Scheme doesn't match a backend, use default backend without rewriting.
  if (!config_->defaultBackendName().empty()) {
    return {config_->defaultBackendName(), uri};
  }

  return {"", uri};
}

std::pair<std::string, std::string> McpRouterFilter::parsePromptName(const std::string& prefixed) {
  // Prompt names use the same "__" delimiter as tool names for backend routing.
  // Example: "time__greeting" -> backend="time", prompt="greeting"
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
  std::string prompt = prefixed.substr(pos + kNameDelimiter.size());

  if (config_->findBackend(backend) != nullptr) {
    return {backend, prompt};
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

ssize_t McpRouterFilter::rewriteResourceUriBody(Buffer::Instance& buffer) {
  if (resource_uri_.empty() || resource_uri_ == rewritten_uri_) {
    return 0;
  }

  // Search for the original URI and replace with the rewritten version.
  ssize_t pos = buffer.search(resource_uri_.data(), resource_uri_.size(), 0);
  if (pos < 0) {
    return 0;
  }

  return rewriteAtPosition(buffer, pos, resource_uri_, rewritten_uri_);
}

ssize_t McpRouterFilter::rewritePromptsGetBody(Buffer::Instance& buffer) {
  if (prompt_name_.empty() || prompt_name_ == unprefixed_prompt_name_) {
    return 0;
  }

  // Search for the prefixed prompt name and replace with the unprefixed version.
  ssize_t pos = buffer.search(prompt_name_.data(), prompt_name_.size(), 0);
  if (pos < 0) {
    return 0;
  }

  return rewriteAtPosition(buffer, pos, prompt_name_, unprefixed_prompt_name_);
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

    // Create per-backend StreamOptions with the backend-specific timeout.
    Http::AsyncClient::StreamOptions backend_options;
    backend_options.setTimeout(backend.timeout);

    stream_callbacks_.push_back(stream_cb);
    mux_callbacks.push_back({
        .cluster_name = backend.cluster_name,
        .callbacks = std::weak_ptr<Http::AsyncClient::StreamCallbacks>(stream_cb),
        .options = backend_options,
    });
  }

  // Default options (used as fallback if per-backend options are not set).
  Http::AsyncClient::StreamOptions default_options;

  auto multistream_or = muxdemux_->multicast(default_options, mux_callbacks);
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
      .options = options,
  });

  Http::AsyncClient::StreamOptions default_options;
  auto multistream_or = muxdemux_->multicast(default_options, mux_callbacks);
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

  // Extract subject for the new session if session identity is configured.
  std::string subject = "default";
  if (config_->hasSessionIdentity()) {
    auto auth_subject = getAuthenticatedSubject();
    if (!auth_subject.ok()) {
      ENVOY_LOG(warn, "Failed to get subject for session: {}", auth_subject.status().message());
      if (config_->shouldEnforceValidation()) {
        sendHttpError(403, "Unable to determine session identity");
        return;
      }
      // In DISABLED mode, proceed with anonymous session.
      ENVOY_LOG(debug, "Subject extraction failed, proceeding with anonymous session");
    } else {
      subject = *auth_subject;
    }
  }

  initializeFanout([this, subject = std::move(subject)](std::vector<BackendResponse> responses) {
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

    std::string composite = SessionCodec::buildCompositeSessionId(route_name_, subject, sessions);
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

void McpRouterFilter::handleResourcesList() {
  ENVOY_LOG(debug, "resources/list: setting up fanout to {} backends", config_->backends().size());

  initializeFanout([this](std::vector<BackendResponse> responses) {
    std::string response_body = aggregateResourcesList(responses);
    ENVOY_LOG(debug, "resources/list: response body: {}", response_body);
    sendJsonResponse(response_body, encoded_session_id_);
  });
}

void McpRouterFilter::handleSingleBackendResourceMethod(absl::string_view method_name) {
  auto [backend_name, actual_uri] = parseResourceUri(resource_uri_);

  if (backend_name.empty()) {
    sendHttpError(
        400, fmt::format("Invalid resource URI '{}': cannot determine backend", resource_uri_));
    return;
  }

  const McpBackendConfig* backend = config_->findBackend(backend_name);
  if (!backend) {
    sendHttpError(400, fmt::format("Unknown backend '{}' in resource URI", backend_name));
    return;
  }

  rewritten_uri_ = actual_uri;
  needs_body_rewrite_ = (resource_uri_ != rewritten_uri_);

  ENVOY_LOG(debug, "{}: backend='{}', uri='{}' -> '{}', needs_rewrite={}", method_name,
            backend_name, resource_uri_, actual_uri, needs_body_rewrite_);

  initializeSingleBackend(*backend, [this](BackendResponse resp) {
    if (resp.success) {
      sendJsonResponse(resp.body, encoded_session_id_);
    } else {
      sendHttpError(500, resp.error.empty() ? "Backend request failed" : resp.error);
    }
  });
}

void McpRouterFilter::handleResourcesRead() { handleSingleBackendResourceMethod("resources/read"); }

void McpRouterFilter::handleResourcesSubscribe() {
  handleSingleBackendResourceMethod("resources/subscribe");
}

void McpRouterFilter::handleResourcesUnsubscribe() {
  handleSingleBackendResourceMethod("resources/unsubscribe");
}

void McpRouterFilter::handlePromptsList() {
  ENVOY_LOG(debug, "prompts/list: setting up fanout to {} backends", config_->backends().size());

  initializeFanout([this](std::vector<BackendResponse> responses) {
    std::string response_body = aggregatePromptsList(responses);
    ENVOY_LOG(debug, "prompts/list: response body: {}", response_body);
    sendJsonResponse(response_body, encoded_session_id_);
  });
}

void McpRouterFilter::handlePromptsGet() {
  auto [backend_name, actual_prompt] = parsePromptName(prompt_name_);

  if (backend_name.empty()) {
    sendHttpError(400,
                  fmt::format("Invalid prompt name '{}': cannot determine backend", prompt_name_));
    return;
  }

  const McpBackendConfig* backend = config_->findBackend(backend_name);
  if (!backend) {
    sendHttpError(400, fmt::format("Unknown backend '{}' in prompt name", backend_name));
    return;
  }

  unprefixed_prompt_name_ = actual_prompt;
  needs_body_rewrite_ = (prompt_name_ != unprefixed_prompt_name_);

  ENVOY_LOG(debug, "prompts/get: backend='{}', prompt='{}' -> '{}', needs_rewrite={}", backend_name,
            prompt_name_, actual_prompt, needs_body_rewrite_);

  initializeSingleBackend(*backend, [this](BackendResponse resp) {
    if (resp.success) {
      sendJsonResponse(resp.body, encoded_session_id_);
    } else {
      sendHttpError(500, resp.error.empty() ? "Backend request failed" : resp.error);
    }
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

namespace {

// Extracts tools from JSON-RPC response, prefixing names if multiplexing.
void extractAndPrefixTools(const std::string& body, absl::string_view backend_name,
                           bool is_multiplexing, std::vector<std::string>& out) {
  const auto parsed = Json::Factory::loadFromString(body);
  if (!parsed.ok()) {
    return;
  }
  const auto result = (*parsed)->getObject("result");
  if (!result.ok() || !*result) {
    return;
  }
  const auto tools = (*result)->getObjectArray("tools");
  if (!tools.ok()) {
    return;
  }

  for (const auto& tool : *tools) {
    if (!tool || !tool->isObject()) {
      continue;
    }
    const auto name = tool->getString("name");
    if (!name.ok()) {
      continue;
    }

    if (!is_multiplexing) {
      // No prefixing needed - use original JSON.
      out.push_back(tool->asJsonString());
      continue;
    }

    // Reconstruct JSON with prefixed name using Json::StringStreamer.
    std::string json;
    Json::StringStreamer streamer(json);
    {
      auto map = streamer.makeRootMap();

      // name - prefix with backend name
      map->addKey("name");
      map->addString(absl::StrCat(backend_name, kNameDelimiter, *name));

      // description
      const auto desc = tool->getString("description");
      if (desc.ok()) {
        map->addKey("description");
        map->addString(*desc);
      }

      // inputSchema
      const auto schema = tool->getObject("inputSchema");
      if (schema.ok() && *schema) {
        map->addKey("inputSchema");
        map->addRawJson((*schema)->asJsonString());
      }

      // annotations
      const auto annotations = tool->getObject("annotations");
      if (annotations.ok() && *annotations) {
        map->addKey("annotations");
        map->addRawJson((*annotations)->asJsonString());
      }

      // icons
      const auto icons = tool->getObjectArray("icons");
      if (icons.ok() && !icons->empty()) {
        map->addKey("icons");
        auto icons_array = map->addArray();
        for (const auto& icon : *icons) {
          icons_array->addRawJson(icon->asJsonString());
        }
      }
    }

    out.push_back(std::move(json));
  }
}

} // namespace

std::string McpRouterFilter::aggregateToolsList(const std::vector<BackendResponse>& responses) {
  std::vector<std::string> all_tools;
  const bool is_multiplexing = config_->isMultiplexing();
  for (const auto& resp : responses) {
    if (!resp.success) {
      continue;
    }
    ENVOY_LOG(debug, "Aggregating tools from backend '{}'", resp.backend_name);
    extractAndPrefixTools(resp.body, resp.backend_name, is_multiplexing, all_tools);
  }

  return absl::StrCat(R"({"jsonrpc":"2.0","id":)", request_id_, R"(,"result":{"tools":[)",
                      absl::StrJoin(all_tools, ","), "]}}");
}

std::string McpRouterFilter::aggregateResourcesList(const std::vector<BackendResponse>& responses) {
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
      result_map->addKey("resources");
      {
        auto resources_array = result_map->addArray();

        for (const auto& resp : responses) {
          if (!resp.success) {
            continue;
          }
          ENVOY_LOG(debug, "Aggregating resources list from backend '{}': {}", resp.backend_name,
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

          auto resources_or = (*result_or)->getObjectArray("resources");
          if (!resources_or.ok()) {
            continue;
          }

          for (const auto& resource : *resources_or) {
            if (!resource || !resource->isObject()) {
              continue;
            }

            auto uri_or = resource->getString("uri");
            if (!uri_or.ok()) {
              continue;
            }

            auto resource_map = resources_array->addMap();

            // Prefix URI with backend name in multiplexing mode using format: backend+scheme://path
            // Example: "file://path" -> "time+file://path" (for backend "time").
            resource_map->addKey("uri");
            if (is_multiplexing) {
              std::string original_uri = *uri_or;
              size_t scheme_end = original_uri.find("://");
              if (scheme_end != std::string::npos) {
                // Insert backend name before the scheme.
                std::string scheme = original_uri.substr(0, scheme_end);
                std::string rest = original_uri.substr(scheme_end); // Includes "://path"
                resource_map->addString(absl::StrCat(resp.backend_name, "+", scheme, rest));
              } else {
                // No scheme, use backend name with empty scheme.
                resource_map->addString(absl::StrCat(resp.backend_name, "+://", original_uri));
              }
            } else {
              resource_map->addString(*uri_or);
            }

            auto name_or = resource->getString("name", "");
            if (name_or.ok() && !name_or->empty()) {
              resource_map->addKey("name");
              resource_map->addString(*name_or);
            }

            auto desc_or = resource->getString("description", "");
            if (desc_or.ok() && !desc_or->empty()) {
              resource_map->addKey("description");
              resource_map->addString(*desc_or);
            }

            auto mime_or = resource->getString("mimeType", "");
            if (mime_or.ok() && !mime_or->empty()) {
              resource_map->addKey("mimeType");
              resource_map->addString(*mime_or);
            }
          }
        }
      }
    }
  }

  return output;
}

std::string McpRouterFilter::aggregatePromptsList(const std::vector<BackendResponse>& responses) {
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
      result_map->addKey("prompts");
      {
        auto prompts_array = result_map->addArray();

        for (const auto& resp : responses) {
          if (!resp.success) {
            continue;
          }
          ENVOY_LOG(debug, "Aggregating prompts list from backend '{}': {}", resp.backend_name,
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

          auto prompts_or = (*result_or)->getObjectArray("prompts");
          if (!prompts_or.ok()) {
            continue;
          }

          for (const auto& prompt : *prompts_or) {
            if (!prompt || !prompt->isObject()) {
              continue;
            }

            auto name_or = prompt->getString("name");
            if (!name_or.ok()) {
              continue;
            }

            auto prompt_map = prompts_array->addMap();

            // Prefix prompt name with backend name in multiplexing mode.
            // Example: "greeting" -> "time__greeting" (for backend "time").
            prompt_map->addKey("name");
            if (is_multiplexing) {
              prompt_map->addString(absl::StrCat(resp.backend_name, kNameDelimiter, *name_or));
            } else {
              prompt_map->addString(*name_or);
            }

            auto desc_or = prompt->getString("description", "");
            if (desc_or.ok() && !desc_or->empty()) {
              prompt_map->addKey("description");
              prompt_map->addString(*desc_or);
            }

            // Handle arguments array if present.
            auto args_or = prompt->getObjectArray("arguments");
            if (args_or.ok() && !args_or->empty()) {
              prompt_map->addKey("arguments");
              auto args_array = prompt_map->addArray();
              for (const auto& arg : *args_or) {
                if (!arg || !arg->isObject()) {
                  continue;
                }
                auto arg_map = args_array->addMap();

                auto arg_name_or = arg->getString("name", "");
                if (arg_name_or.ok() && !arg_name_or->empty()) {
                  arg_map->addKey("name");
                  arg_map->addString(*arg_name_or);
                }

                auto arg_desc_or = arg->getString("description", "");
                if (arg_desc_or.ok() && !arg_desc_or->empty()) {
                  arg_map->addKey("description");
                  arg_map->addString(*arg_desc_or);
                }

                auto required_or = arg->getBoolean("required", false);
                if (required_or.ok()) {
                  arg_map->addKey("required");
                  arg_map->addBool(*required_or);
                }
              }
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

    // Adjust content-length when body rewriting changes size.
    if (needs_body_rewrite_ && request_headers_->ContentLength()) {
      uint64_t original_length = 0;
      if (absl::SimpleAtoi(request_headers_->getContentLengthValue(), &original_length)) {
        int64_t size_delta = 0;
        if (method_ == McpMethod::ToolsCall) {
          // Tool name rewriting, delta = new_size - old_size.
          size_delta = static_cast<int64_t>(unprefixed_tool_name_.size()) -
                       static_cast<int64_t>(tool_name_.size());
        } else if (method_ == McpMethod::ResourcesRead ||
                   method_ == McpMethod::ResourcesSubscribe ||
                   method_ == McpMethod::ResourcesUnsubscribe) {
          // Resource URI rewriting, delta = new_size - old_size.
          size_delta = static_cast<int64_t>(rewritten_uri_.size()) -
                       static_cast<int64_t>(resource_uri_.size());
        } else if (method_ == McpMethod::PromptsGet) {
          // Prompt name rewriting, delta = new_size - old_size.
          size_delta = static_cast<int64_t>(unprefixed_prompt_name_.size()) -
                       static_cast<int64_t>(prompt_name_.size());
        }
        int64_t new_length = static_cast<int64_t>(original_length) + size_delta;
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
