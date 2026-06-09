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

constexpr absl::string_view kContentTypeJson = "application/json";
constexpr absl::string_view kContentTypeSse = "text/event-stream";

void copyRequestHeaders(const Http::RequestHeaderMap& source, Http::RequestHeaderMap& dest) {
  // Headers that we set explicitly or should not be forwarded
  static const absl::flat_hash_set<absl::string_view> kSkipHeaders = {
      ":method", ":path", ":authority", "host", "content-type", "accept", kSessionIdHeader};

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
  CONSTRUCT_ON_FIRST_USE(
      McpMethodMap, {{"initialize", McpMethod::Initialize},
                     {"tools/list", McpMethod::ToolsList},
                     {"tools/call", McpMethod::ToolsCall},
                     {"resources/list", McpMethod::ResourcesList},
                     {"resources/read", McpMethod::ResourcesRead},
                     {"resources/subscribe", McpMethod::ResourcesSubscribe},
                     {"resources/unsubscribe", McpMethod::ResourcesUnsubscribe},
                     {"resources/templates/list", McpMethod::ResourcesTemplatesList},
                     {"prompts/list", McpMethod::PromptsList},
                     {"prompts/get", McpMethod::PromptsGet},
                     {"completion/complete", McpMethod::CompletionComplete},
                     {"logging/setLevel", McpMethod::LoggingSetLevel},
                     {"ping", McpMethod::Ping},
                     // Notifications (client -> server).
                     {"notifications/initialized", McpMethod::NotificationInitialized},
                     {"notifications/cancelled", McpMethod::NotificationCancelled},
                     {"notifications/roots/list_changed", McpMethod::NotificationRootsListChanged},
                     {"__jsonrpc_response", McpMethod::ServerResponse}});
}

McpMethod parseMethodString(absl::string_view method_str) {
  auto it = mcpMethodMap().find(method_str);
  return it != mcpMethodMap().end() ? it->second : McpMethod::Unknown;
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
  aggregation_callback_ = nullptr;
  single_backend_callback_ = nullptr;
}

absl::StatusOr<envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig>
McpRouterFilter::getClusterConfig() {
  OptRef<const Upstream::ClusterInfo> cluster_info = decoder_callbacks_->clusterInfo();
  if (!cluster_info.has_value()) {
    return absl::NotFoundError("No cluster info");
  }

  const auto& typed_filter_metadata = cluster_info.ref().metadata().typed_filter_metadata();
  auto it = typed_filter_metadata.find("envoy.clusters.mcp_multicluster");
  if (it == typed_filter_metadata.end()) {
    return absl::NotFoundError(
        fmt::format("Metadata for 'envoy.clusters.mcp_multicluster' not found in cluster '{}'",
                    cluster_info.ref().name()));
  }

  envoy::extensions::clusters::mcp_multicluster::v3::ClusterConfig cluster_config;
  if (!it->second.UnpackTo(&cluster_config)) {
    return absl::InvalidArgumentError("Failed to parse ClusterConfig metadata: {}");
  }

  return cluster_config;
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

  auto cluster_config_or = getClusterConfig();
  if (cluster_config_or.ok()) {
    config_ =
        std::make_shared<McpRouterClusterConfigImpl>(cluster_config_or.value(), std::move(config_));
  }

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
    config_->stats().rq_total_.inc();

    if (!readMetadataFromMcpFilter()) {
      config_->stats().rq_invalid_.inc();
      sendHttpError(400, "Invalid or missing MCP request");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    ENVOY_LOG(debug, "MCP router: method={}, request_id={}", static_cast<int>(method_),
              request_id_);

    if (!encoded_session_id_.empty() && !decodeAndParseSession()) {
      // decodeAndParseSession already sent the appropriate error response.
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

    case McpMethod::ResourcesTemplatesList:
      handleResourcesTemplatesList();
      break;

    case McpMethod::PromptsList:
      handlePromptsList();
      break;

    case McpMethod::PromptsGet:
      handlePromptsGet();
      break;

    case McpMethod::CompletionComplete:
      handleCompletionComplete();
      break;

    case McpMethod::LoggingSetLevel:
      handleLoggingSetLevel();
      break;

    case McpMethod::Ping:
      handlePing();
      return Http::FilterDataStatus::StopIterationNoBuffer;

    case McpMethod::NotificationInitialized:
      handleNotification("notifications/initialized");
      break;

    case McpMethod::NotificationCancelled:
      handleNotification("notifications/cancelled");
      break;

    case McpMethod::NotificationRootsListChanged:
      handleNotification("notifications/roots/list_changed");
      break;

    case McpMethod::ServerResponse:
      handleServerResponse();
      break;

    default:
      config_->stats().rq_invalid_.inc();
      sendHttpError(400, "Unsupported method");
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    initialized_ = true;

    // If lazy init is in progress, buffer data — the completion callback will replay it.
    if (lazy_init_pending_) {
      lazy_init_request_body_.move(data);
      return Http::FilterDataStatus::StopIterationNoBuffer;
    }

    // Perform body rewriting if needed (e.g., tool/prompt name or URI prefix stripping).
    // This is done once on the first data chunk after initialization.
    if (needs_body_rewrite_) {
      config_->stats().rq_body_rewrite_.inc();
      if (method_ == McpMethod::ToolsCall) {
        rewriteToolCallBody(data);
      } else if (method_ == McpMethod::ResourcesRead || method_ == McpMethod::ResourcesSubscribe ||
                 method_ == McpMethod::ResourcesUnsubscribe) {
        rewriteResourceUriBody(data);
      } else if (method_ == McpMethod::PromptsGet) {
        rewritePromptsGetBody(data);
      } else if (method_ == McpMethod::CompletionComplete) {
        rewriteCompletionCompleteBody(data);
      } else if (method_ == McpMethod::ServerResponse) {
        rewriteServerResponseId(data);
      }
      needs_body_rewrite_ = false;
    }
  }

  // If lazy init is in progress, buffer data — the completion callback will replay it.
  if (lazy_init_pending_) {
    lazy_init_request_body_.move(data);
    return Http::FilterDataStatus::StopIterationNoBuffer;
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

  auto filter_it = metadata.filter_metadata().find(config_->metadataNamespace());
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

  // Extract request ID (numeric for requests, string for server responses).
  auto id_it = fields.find("id");
  if (id_it != fields.end()) {
    if (id_it->second.has_number_value()) {
      request_id_ = static_cast<int64_t>(id_it->second.number_value());
    } else if (id_it->second.has_string_value()) {
      server_response_id_ = id_it->second.string_value();
    }
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
    } else if (method_ == McpMethod::CompletionComplete) {
      // Extract ref object with type, name (for prompts), and uri (for resources).
      auto ref_it = params_fields.find("ref");
      if (ref_it != params_fields.end() && ref_it->second.has_struct_value()) {
        const auto& ref_fields = ref_it->second.struct_value().fields();

        auto type_it = ref_fields.find("type");
        if (type_it != ref_fields.end() && type_it->second.has_string_value()) {
          completion_ref_type_ = type_it->second.string_value();
        }

        if (completion_ref_type_ == "ref/prompt") {
          auto name_it = ref_fields.find("name");
          if (name_it != ref_fields.end() && name_it->second.has_string_value()) {
            prompt_name_ = name_it->second.string_value();
          }
        } else if (completion_ref_type_ == "ref/resource") {
          auto uri_it = ref_fields.find("uri");
          if (uri_it != ref_fields.end() && uri_it->second.has_string_value()) {
            resource_uri_ = uri_it->second.string_value();
          }
        }
      }
    }
  }

  return true;
}

bool McpRouterFilter::decodeAndParseSession() {
  std::string decoded = SessionCodec::decode(encoded_session_id_);
  if (decoded.empty()) {
    ENVOY_LOG(warn, "Failed to decode session ID");
    config_->stats().rq_session_invalid_.inc();
    sendHttpError(400, "Invalid session ID");
    return false;
  }

  auto parsed = SessionCodec::parseCompositeSessionId(decoded);
  if (!parsed.ok()) {
    ENVOY_LOG(warn, "Failed to parse session: {}", parsed.status().message());
    config_->stats().rq_session_invalid_.inc();
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
    config_->stats().rq_auth_failure_.inc();
    sendHttpError(403, "Unable to verify session identity");
    return false;
  }

  if (session_subject_ != *auth_subject) {
    ENVOY_LOG(warn, "Session subject mismatch: session='{}'", session_subject_);
    config_->stats().rq_auth_failure_.inc();
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

ssize_t McpRouterFilter::rewriteCompletionCompleteBody(Buffer::Instance& buffer) {
  // Rewrite based on ref type: prompt name for ref/prompt, resource URI for ref/resource.
  if (completion_ref_type_ == "ref/prompt") {
    return rewritePromptsGetBody(buffer);
  } else if (completion_ref_type_ == "ref/resource") {
    return rewriteResourceUriBody(buffer);
  }
  return 0;
}

std::pair<std::string, std::string>
McpRouterFilter::parseServerResponseId(const std::string& prefixed_id) {
  if (!config_->isMultiplexing()) {
    return {config_->defaultBackendName(), prefixed_id};
  }

  size_t pos = prefixed_id.find(kNameDelimiter);
  if (pos == std::string::npos) {
    if (!config_->defaultBackendName().empty()) {
      return {config_->defaultBackendName(), prefixed_id};
    }
    return {"", prefixed_id};
  }

  std::string backend = prefixed_id.substr(0, pos);
  std::string original_id = prefixed_id.substr(pos + kNameDelimiter.size());

  if (config_->findBackend(backend) != nullptr) {
    return {backend, original_id};
  }

  return {"", prefixed_id};
}

ssize_t McpRouterFilter::rewriteServerResponseId(Buffer::Instance& buffer) {
  if (server_response_id_.empty() || server_response_id_ == original_response_id_) {
    return 0;
  }

  // The prefixed ID is a quoted string in the JSON body: "id":"time__42".
  // We need to replace it with the original ID, restoring numeric type if possible.
  std::string search_str = absl::StrCat("\"", server_response_id_, "\"");
  ssize_t pos = buffer.search(search_str.data(), search_str.size(), 0);
  if (pos < 0) {
    return 0;
  }

  // If the original ID is all digits, output as unquoted number to restore type.
  bool is_numeric = !original_response_id_.empty() &&
                    std::all_of(original_response_id_.begin(), original_response_id_.end(),
                                [](char c) { return c >= '0' && c <= '9'; });
  std::string replacement =
      is_numeric ? original_response_id_ : absl::StrCat("\"", original_response_id_, "\"");

  return rewriteAtPosition(buffer, pos, search_str, replacement);
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

void McpRouterFilter::initializeFanout(AggregationCallback callback,
                                       const std::vector<std::string>& target_backends) {
  config_->stats().rq_fanout_.inc();
  if (config_->backends().empty()) {
    sendHttpError(500, "No backends configured");
    return;
  }

  if (!muxdemux_->isIdle()) {
    ENVOY_LOG(warn, "MuxDemux not idle, cannot start new fanout");
    sendHttpError(500, "Internal error: concurrent fanout not allowed");
    return;
  }

  // Build list of backends to fan out to.
  absl::flat_hash_set<std::string> target_set(target_backends.begin(), target_backends.end());
  std::vector<const McpBackendConfig*> backends_to_use;
  for (const auto& backend : config_->backends()) {
    if (target_set.empty() || target_set.contains(backend.name)) {
      backends_to_use.push_back(&backend);
    }
  }

  size_t expected = backends_to_use.size();
  if (expected == 0) {
    sendHttpError(500, "No matching backends for fanout");
    return;
  }

  pending_responses_ = std::make_shared<std::vector<BackendResponse>>();
  pending_responses_->reserve(expected);
  response_count_ = std::make_shared<size_t>(0);
  aggregation_callback_ = std::move(callback);

  std::vector<Http::MuxDemux::Callbacks> mux_callbacks;
  mux_callbacks.reserve(expected);

  for (const auto* backend : backends_to_use) {
    auto responses = pending_responses_;
    auto count = response_count_;
    auto expected_count = expected;
    auto agg_callback = aggregation_callback_;

    auto stream_cb = std::make_shared<BackendStreamCallbacks>(
        backend->name,
        [responses, count, expected_count, agg_callback](BackendResponse resp) {
          responses->push_back(std::move(resp));
          if (++(*count) >= expected_count && agg_callback) {
            agg_callback(std::move(*responses));
          }
        },
        request_id_, true /* aggregate_mode */, weak_from_this());

    Http::AsyncClient::StreamOptions backend_options;
    backend_options.setTimeout(backend->timeout);

    stream_callbacks_.push_back(stream_cb);
    mux_callbacks.push_back({
        .cluster_name = backend->cluster_name,
        .callbacks = std::weak_ptr<Http::AsyncClient::StreamCallbacks>(stream_cb),
        .options = backend_options,
    });
  }

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

  auto stream_it = multistream_->begin();
  for (const auto* backend : backends_to_use) {
    if (stream_it == multistream_->end()) {
      break;
    }

    auto headers = createUpstreamHeaders(*backend, backend_sessions_[backend->name]);
    upstream_headers_.push_back(std::move(headers));
    (*stream_it)->sendHeaders(*upstream_headers_.back(), false);
    ++stream_it;
  }
}

void McpRouterFilter::initializeSingleBackend(const McpBackendConfig& backend,
                                              std::function<void(BackendResponse)> callback) {
  initializeSingleBackend(backend, std::move(callback), false);
}

void McpRouterFilter::initializeSingleBackend(const McpBackendConfig& backend,
                                              std::function<void(BackendResponse)> callback,
                                              bool streaming_enabled) {
  if (!muxdemux_->isIdle()) {
    ENVOY_LOG(warn, "MuxDemux not idle, cannot start new single backend request for '{}'",
              backend.name);
    sendHttpError(500,
                  fmt::format("Internal error: concurrent request not allowed for backend '{}'",
                              backend.name));
    return;
  }

  single_backend_callback_ = std::move(callback);
  auto stream_cb = std::make_shared<BackendStreamCallbacks>(backend.name, single_backend_callback_,
                                                            request_id_, false /* aggregate_mode */,
                                                            weak_from_this(), streaming_enabled);
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

void McpRouterFilter::pushSseHeaders(Http::ResponseHeaderMapPtr&& headers, bool end_stream) {
  // Remove backend's session ID and replace with the request's session ID.
  headers->remove(Http::LowerCaseString("mcp-session-id"));
  if (!encoded_session_id_.empty()) {
    headers->addCopy(Http::LowerCaseString("mcp-session-id"), encoded_session_id_);
  }

  ENVOY_LOG(debug, "SSE streaming: forwarding headers to client, end_stream={}", end_stream);
  decoder_callbacks_->encodeHeaders(std::move(headers), end_stream, "mcp_router");
}

void McpRouterFilter::pushSseData(Buffer::Instance& data, bool end_stream) {
  ENVOY_LOG(debug, "SSE streaming: forwarding {} bytes, end_stream={}", data.length(), end_stream);
  decoder_callbacks_->encodeData(data, end_stream);
}

void McpRouterFilter::pushSseEvent(const std::string& backend_name, const std::string& event_data,
                                   SseMessageType event_type) {
  ENVOY_LOG(debug, "SSE aggregation: forwarding {} event from backend '{}' (size {})",
            event_type == SseMessageType::Notification ? "notification" : "server_request",
            backend_name, event_data.size());

  // Send SSE headers on first intermediate event (converts response from JSON to SSE).
  if (!sse_headers_sent_) {
    auto headers = Http::ResponseHeaderMapImpl::create();
    headers->setStatus(200);
    headers->setContentType("text/event-stream");
    headers->addCopy(Http::LowerCaseString("cache-control"), "no-cache");
    if (!encoded_session_id_.empty()) {
      headers->addCopy(Http::LowerCaseString("mcp-session-id"), encoded_session_id_);
    }
    ENVOY_LOG(debug, "SSE aggregation: sending SSE headers to client");
    decoder_callbacks_->encodeHeaders(std::move(headers), false, "mcp_router");
    sse_headers_sent_ = true;
  }

  std::string modified_data = event_data;

  // For ServerRequest events in multiplexing mode, prefix the ID with backend name
  // so client responses can be routed back to the correct backend.
  if (event_type == SseMessageType::ServerRequest && config_->isMultiplexing()) {
    auto parsed_or = Json::Factory::loadFromString(event_data);
    if (parsed_or.ok()) {
      // Build the old and new ID value strings, then search for "id":<old>
      // (with optional space after colon).
      std::string old_val;
      std::string new_val;
      auto id_int_or = (*parsed_or)->getInteger("id");
      if (id_int_or.ok()) {
        old_val = std::to_string(*id_int_or);
        new_val = absl::StrCat("\"", backend_name, kNameDelimiter, old_val, "\"");
      } else {
        auto id_str_or = (*parsed_or)->getString("id");
        if (id_str_or.ok()) {
          old_val = absl::StrCat("\"", *id_str_or, "\"");
          new_val = absl::StrCat("\"", backend_name, kNameDelimiter, *id_str_or, "\"");
        }
      }
      if (!old_val.empty()) {
        // Try "id":VALUE then "id": VALUE (with space).
        std::string search = absl::StrCat("\"id\":", old_val);
        std::string replace = absl::StrCat("\"id\":", new_val);
        size_t pos = modified_data.find(search);
        if (pos == std::string::npos) {
          search = absl::StrCat("\"id\": ", old_val);
          replace = absl::StrCat("\"id\": ", new_val);
          pos = modified_data.find(search);
        }
        if (pos != std::string::npos) {
          modified_data.replace(pos, search.size(), replace);
        }
      }
    }
  }

  // Format and send the SSE event to client.
  Buffer::OwnedImpl buffer;
  buffer.add("event: message\ndata: ");
  buffer.add(modified_data);
  buffer.add("\n\n");
  decoder_callbacks_->encodeData(buffer, false);
}

void McpRouterFilter::onStreamingError(absl::string_view error) {
  ENVOY_LOG(warn, "SSE streaming error: {}", error);
  sendHttpError(500, std::string(error));
}

void McpRouterFilter::onStreamingComplete() { ENVOY_LOG(debug, "SSE streaming: complete"); }

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
      ENVOY_LOG(debug, "Subject extraction failed, proceeding with anonymous session");
    } else {
      subject = *auth_subject;
    }
  }

  if (config_->lazyInitialization()) {
    ENVOY_LOG(debug, "Lazy init: responding immediately without backend fanout");

    session_subject_ = subject;
    std::vector<BackendResponse> empty_responses;
    std::string response_body = aggregateInitialize(empty_responses);

    absl::flat_hash_map<std::string, std::string> empty_sessions;
    std::string composite =
        SessionCodec::buildCompositeSessionId(route_name_, subject, empty_sessions);
    std::string encoded_session = SessionCodec::encode(composite);
    encoded_session_id_ = encoded_session;

    sendJsonResponse(response_body, encoded_session);
    return;
  }

  initializeFanout([weak_self = weak_from_this(),
                    subject = std::move(subject)](std::vector<BackendResponse> responses) {
    auto self = weak_self.lock();
    if (!self) {
      ENVOY_LOG(debug, "Initialize callback ignored: filter destroyed");
      return;
    }
    std::string response_body = self->aggregateInitialize(responses);

    // Collect session IDs from successful backends that returned one.
    absl::flat_hash_map<std::string, std::string> sessions;
    bool any_success = false;
    for (const auto& resp : responses) {
      if (resp.success) {
        any_success = true;
        if (!resp.session_id.empty()) {
          sessions[resp.backend_name] = resp.session_id;
        }
      }
    }

    if (!any_success) {
      self->sendHttpError(500, "All backends failed to initialize");
      return;
    }

    std::string encoded_session;
    if (!sessions.empty()) {
      std::string composite =
          SessionCodec::buildCompositeSessionId(self->route_name_, subject, sessions);
      encoded_session = SessionCodec::encode(composite);
    }

    self->sendJsonResponse(response_body, encoded_session);
  });
}

void McpRouterFilter::handlePing() {
  config_->stats().rq_direct_response_.inc();
  ENVOY_LOG(debug, "Ping: responding immediately with empty result");

  // Ping is a request/response pattern - respond immediately with empty result.
  // Per MCP spec: The receiver MUST respond promptly with an empty response.
  std::string response_body =
      absl::StrCat(R"({"jsonrpc":"2.0","id":)", request_id_, R"(,"result":{}})");
  sendJsonResponse(response_body, encoded_session_id_);
}

void McpRouterFilter::handleNotification(absl::string_view notification_name) {
  config_->stats().rq_direct_response_.inc();
  ENVOY_LOG(debug, "{}: forwarding to {} backends", notification_name, config_->backends().size());

  if (config_->lazyInitialization() && initialized_backends_.empty()) {
    ENVOY_LOG(debug, "{}: no backends initialized yet (lazy init), responding 202",
              notification_name);
    sendAccepted();
    return;
  }

  std::vector<std::string> targets;
  if (config_->lazyInitialization()) {
    targets.assign(initialized_backends_.begin(), initialized_backends_.end());
  }

  initializeFanout(
      [weak_self = weak_from_this()](std::vector<BackendResponse>) {
        auto self = weak_self.lock();
        if (!self) {
          ENVOY_LOG(debug, "notification callback ignored: filter destroyed");
          return;
        }
        self->sendAccepted();
      },
      targets);
}

void McpRouterFilter::handleToolsList() {
  ENVOY_LOG(debug, "tools/list: setting up fanout to {} backends", config_->backends().size());

  auto start_tools_list = [this]() {
    initializeFanout([weak_self = weak_from_this()](std::vector<BackendResponse> responses) {
      auto self = weak_self.lock();
      if (!self) {
        ENVOY_LOG(debug, "tools/list callback ignored: filter destroyed");
        return;
      }
      std::string response_body = self->aggregateToolsList(responses);
      ENVOY_LOG(debug, "tools/list: response body: {}", response_body);
      self->sendJsonResponse(response_body, self->encoded_session_id_);
    });

    if (lazy_init_request_body_.length() > 0) {
      streamData(lazy_init_request_body_, true);
    }
  };

  if (config_->lazyInitialization() && initialized_backends_.size() < config_->backends().size()) {
    lazy_init_pending_ = true;
    lazyInitFanout([weak_self = weak_from_this(),
                    start_tools_list = std::move(start_tools_list)](bool success) {
      auto self = weak_self.lock();
      if (!self) {
        return;
      }
      self->lazy_init_pending_ = false;
      if (!success) {
        self->config_->stats().rq_fanout_failure_.inc();
        self->sendHttpError(500, "Failed to initialize backends");
        return;
      }
      start_tools_list();
    });
    return;
  }

  start_tools_list();
}

void McpRouterFilter::handleToolsCall() {
  auto [backend_name, actual_tool] = parseToolName(tool_name_);

  if (backend_name.empty()) {
    config_->stats().rq_unknown_backend_.inc();
    sendHttpError(400, fmt::format("Invalid tool name '{}': cannot determine backend", tool_name_));
    return;
  }

  const McpBackendConfig* backend = config_->findBackend(backend_name);
  if (!backend) {
    config_->stats().rq_unknown_backend_.inc();
    sendHttpError(400, fmt::format("Unknown backend '{}' in tool name", backend_name));
    return;
  }

  unprefixed_tool_name_ = actual_tool;
  needs_body_rewrite_ = (tool_name_ != unprefixed_tool_name_);

  ENVOY_LOG(debug, "tools/call: backend='{}', tool='{}' -> '{}', needs_rewrite={}", backend_name,
            tool_name_, actual_tool, needs_body_rewrite_);

  auto start_tools_call = [this, backend]() {
    initializeSingleBackend(
        *backend,
        [weak_self = weak_from_this()](BackendResponse resp) {
          auto self = weak_self.lock();
          if (!self) {
            return;
          }
          if (resp.success) {
            if (resp.isSse()) {
              ENVOY_LOG(warn, "tools/call: SSE response reached non-streaming path");
              self->sendHttpError(500, "Internal error: streaming failed for SSE response");
            } else {
              self->sendJsonResponse(resp.body, self->encoded_session_id_);
            }
          } else {
            self->config_->stats().rq_backend_failure_.inc();
            self->sendHttpError(500, resp.error.empty() ? "Backend request failed" : resp.error);
          }
        },
        true /* streaming_enabled */);

    // Replay buffered data if we came from lazy init.
    if (lazy_init_request_body_.length() > 0) {
      if (needs_body_rewrite_) {
        config_->stats().rq_body_rewrite_.inc();
        rewriteToolCallBody(lazy_init_request_body_);
        needs_body_rewrite_ = false;
      }
      streamData(lazy_init_request_body_, true);
    }
  };

  if (config_->lazyInitialization() && !initialized_backends_.contains(backend_name)) {
    lazy_init_pending_ = true;
    lazyInitSingleBackend(*backend, [weak_self = weak_from_this(),
                                     start_tools_call = std::move(start_tools_call),
                                     backend_name](bool success) {
      auto self = weak_self.lock();
      if (!self) {
        return;
      }
      self->lazy_init_pending_ = false;
      if (!success) {
        self->config_->stats().rq_backend_failure_.inc();
        self->sendHttpError(500, fmt::format("Failed to initialize backend '{}'", backend_name));
        return;
      }
      start_tools_call();
    });
    return;
  }

  start_tools_call();
}

void McpRouterFilter::handleResourcesList() {
  ENVOY_LOG(debug, "resources/list: setting up fanout to {} backends", config_->backends().size());

  auto start_resources_list = [this]() {
    initializeFanout([weak_self = weak_from_this()](std::vector<BackendResponse> responses) {
      auto self = weak_self.lock();
      if (!self) {
        ENVOY_LOG(debug, "resources/list callback ignored: filter destroyed");
        return;
      }
      std::string response_body = self->aggregateResourcesList(responses);
      ENVOY_LOG(debug, "resources/list: response body: {}", response_body);
      self->sendJsonResponse(response_body, self->encoded_session_id_);
    });

    if (lazy_init_request_body_.length() > 0) {
      streamData(lazy_init_request_body_, true);
    }
  };

  if (config_->lazyInitialization() && initialized_backends_.size() < config_->backends().size()) {
    lazy_init_pending_ = true;
    lazyInitFanout([weak_self = weak_from_this(),
                    start_resources_list = std::move(start_resources_list)](bool success) {
      auto self = weak_self.lock();
      if (!self) {
        return;
      }
      self->lazy_init_pending_ = false;
      if (!success) {
        self->config_->stats().rq_fanout_failure_.inc();
        self->sendHttpError(500, "Failed to initialize backends");
        return;
      }
      start_resources_list();
    });
    return;
  }

  start_resources_list();
}

void McpRouterFilter::handleSingleBackendResourceMethod(absl::string_view method_name) {
  auto [backend_name, actual_uri] = parseResourceUri(resource_uri_);

  if (backend_name.empty()) {
    config_->stats().rq_unknown_backend_.inc();
    sendHttpError(
        400, fmt::format("Invalid resource URI '{}': cannot determine backend", resource_uri_));
    return;
  }

  const McpBackendConfig* backend = config_->findBackend(backend_name);
  if (!backend) {
    config_->stats().rq_unknown_backend_.inc();
    sendHttpError(400, fmt::format("Unknown backend '{}' in resource URI", backend_name));
    return;
  }

  rewritten_uri_ = actual_uri;
  needs_body_rewrite_ = (resource_uri_ != rewritten_uri_);

  ENVOY_LOG(debug, "{}: backend='{}', uri='{}' -> '{}', needs_rewrite={}", method_name,
            backend_name, resource_uri_, actual_uri, needs_body_rewrite_);

  auto start_resource_method = [this, backend]() {
    initializeSingleBackend(*backend, [weak_self = weak_from_this()](BackendResponse resp) {
      auto self = weak_self.lock();
      if (!self) {
        return;
      }
      if (resp.success) {
        self->sendJsonResponse(resp.body, self->encoded_session_id_);
      } else {
        self->config_->stats().rq_backend_failure_.inc();
        self->sendHttpError(500, resp.error.empty() ? "Backend request failed" : resp.error);
      }
    });

    if (lazy_init_request_body_.length() > 0) {
      if (needs_body_rewrite_) {
        config_->stats().rq_body_rewrite_.inc();
        rewriteResourceUriBody(lazy_init_request_body_);
        needs_body_rewrite_ = false;
      }
      streamData(lazy_init_request_body_, true);
    }
  };

  if (config_->lazyInitialization() && !initialized_backends_.contains(backend_name)) {
    lazy_init_pending_ = true;
    lazyInitSingleBackend(*backend, [weak_self = weak_from_this(),
                                     start_resource_method = std::move(start_resource_method),
                                     backend_name](bool success) {
      auto self = weak_self.lock();
      if (!self) {
        return;
      }
      self->lazy_init_pending_ = false;
      if (!success) {
        self->config_->stats().rq_backend_failure_.inc();
        self->sendHttpError(500, fmt::format("Failed to initialize backend '{}'", backend_name));
        return;
      }
      start_resource_method();
    });
    return;
  }

  start_resource_method();
}

void McpRouterFilter::handleResourcesRead() { handleSingleBackendResourceMethod("resources/read"); }

void McpRouterFilter::handleResourcesSubscribe() {
  handleSingleBackendResourceMethod("resources/subscribe");
}

void McpRouterFilter::handleResourcesUnsubscribe() {
  handleSingleBackendResourceMethod("resources/unsubscribe");
}

void McpRouterFilter::handleResourcesTemplatesList() {
  ENVOY_LOG(debug, "resources/templates/list: setting up fanout to {} backends",
            config_->backends().size());

  auto start_templates_list = [this]() {
    initializeFanout([weak_self = weak_from_this()](std::vector<BackendResponse> responses) {
      auto self = weak_self.lock();
      if (!self) {
        ENVOY_LOG(debug, "resources/templates/list callback ignored: filter destroyed");
        return;
      }
      std::string response_body = self->aggregateResourcesTemplatesList(responses);
      ENVOY_LOG(debug, "resources/templates/list: response body: {}", response_body);
      self->sendJsonResponse(response_body, self->encoded_session_id_);
    });

    if (lazy_init_request_body_.length() > 0) {
      streamData(lazy_init_request_body_, true);
    }
  };

  if (config_->lazyInitialization() && initialized_backends_.size() < config_->backends().size()) {
    lazy_init_pending_ = true;
    lazyInitFanout([weak_self = weak_from_this(),
                    start_templates_list = std::move(start_templates_list)](bool success) {
      auto self = weak_self.lock();
      if (!self) {
        return;
      }
      self->lazy_init_pending_ = false;
      if (!success) {
        self->config_->stats().rq_fanout_failure_.inc();
        self->sendHttpError(500, "Failed to initialize backends");
        return;
      }
      start_templates_list();
    });
    return;
  }

  start_templates_list();
}

void McpRouterFilter::handlePromptsList() {
  ENVOY_LOG(debug, "prompts/list: setting up fanout to {} backends", config_->backends().size());

  auto start_prompts_list = [this]() {
    initializeFanout([weak_self = weak_from_this()](std::vector<BackendResponse> responses) {
      auto self = weak_self.lock();
      if (!self) {
        ENVOY_LOG(debug, "prompts/list callback ignored: filter destroyed");
        return;
      }
      std::string response_body = self->aggregatePromptsList(responses);
      ENVOY_LOG(debug, "prompts/list: response body: {}", response_body);
      self->sendJsonResponse(response_body, self->encoded_session_id_);
    });

    if (lazy_init_request_body_.length() > 0) {
      streamData(lazy_init_request_body_, true);
    }
  };

  if (config_->lazyInitialization() && initialized_backends_.size() < config_->backends().size()) {
    lazy_init_pending_ = true;
    lazyInitFanout([weak_self = weak_from_this(),
                    start_prompts_list = std::move(start_prompts_list)](bool success) {
      auto self = weak_self.lock();
      if (!self) {
        return;
      }
      self->lazy_init_pending_ = false;
      if (!success) {
        self->config_->stats().rq_fanout_failure_.inc();
        self->sendHttpError(500, "Failed to initialize backends");
        return;
      }
      start_prompts_list();
    });
    return;
  }

  start_prompts_list();
}

void McpRouterFilter::handlePromptsGet() {
  auto [backend_name, actual_prompt] = parsePromptName(prompt_name_);

  if (backend_name.empty()) {
    config_->stats().rq_unknown_backend_.inc();
    sendHttpError(400,
                  fmt::format("Invalid prompt name '{}': cannot determine backend", prompt_name_));
    return;
  }

  const McpBackendConfig* backend = config_->findBackend(backend_name);
  if (!backend) {
    config_->stats().rq_unknown_backend_.inc();
    sendHttpError(400, fmt::format("Unknown backend '{}' in prompt name", backend_name));
    return;
  }

  unprefixed_prompt_name_ = actual_prompt;
  needs_body_rewrite_ = (prompt_name_ != unprefixed_prompt_name_);

  ENVOY_LOG(debug, "prompts/get: backend='{}', prompt='{}' -> '{}', needs_rewrite={}", backend_name,
            prompt_name_, actual_prompt, needs_body_rewrite_);

  auto start_prompts_get = [this, backend]() {
    initializeSingleBackend(*backend, [weak_self = weak_from_this()](BackendResponse resp) {
      auto self = weak_self.lock();
      if (!self) {
        return;
      }
      if (resp.success) {
        self->sendJsonResponse(resp.body, self->encoded_session_id_);
      } else {
        self->config_->stats().rq_backend_failure_.inc();
        self->sendHttpError(500, resp.error.empty() ? "Backend request failed" : resp.error);
      }
    });

    if (lazy_init_request_body_.length() > 0) {
      if (needs_body_rewrite_) {
        config_->stats().rq_body_rewrite_.inc();
        rewritePromptsGetBody(lazy_init_request_body_);
        needs_body_rewrite_ = false;
      }
      streamData(lazy_init_request_body_, true);
    }
  };

  if (config_->lazyInitialization() && !initialized_backends_.contains(backend_name)) {
    lazy_init_pending_ = true;
    lazyInitSingleBackend(*backend, [weak_self = weak_from_this(),
                                     start_prompts_get = std::move(start_prompts_get),
                                     backend_name](bool success) {
      auto self = weak_self.lock();
      if (!self) {
        return;
      }
      self->lazy_init_pending_ = false;
      if (!success) {
        self->config_->stats().rq_backend_failure_.inc();
        self->sendHttpError(500, fmt::format("Failed to initialize backend '{}'", backend_name));
        return;
      }
      start_prompts_get();
    });
    return;
  }

  start_prompts_get();
}

void McpRouterFilter::handleCompletionComplete() {
  std::string backend_name;

  if (completion_ref_type_ == "ref/prompt") {
    auto [name, actual_prompt] = parsePromptName(prompt_name_);
    backend_name = name;
    unprefixed_prompt_name_ = actual_prompt;
    needs_body_rewrite_ = (prompt_name_ != unprefixed_prompt_name_);

    ENVOY_LOG(debug,
              "completion/complete (ref/prompt): backend='{}', name='{}' -> '{}', "
              "needs_rewrite={}",
              backend_name, prompt_name_, actual_prompt, needs_body_rewrite_);
  } else if (completion_ref_type_ == "ref/resource") {
    auto [name, actual_uri] = parseResourceUri(resource_uri_);
    backend_name = name;
    rewritten_uri_ = actual_uri;
    needs_body_rewrite_ = (resource_uri_ != rewritten_uri_);

    ENVOY_LOG(debug,
              "completion/complete (ref/resource): backend='{}', uri='{}' -> '{}', "
              "needs_rewrite={}",
              backend_name, resource_uri_, actual_uri, needs_body_rewrite_);
  } else {
    sendHttpError(400, fmt::format("Invalid completion ref type '{}'", completion_ref_type_));
    return;
  }

  if (backend_name.empty()) {
    config_->stats().rq_unknown_backend_.inc();
    sendHttpError(400, "Cannot determine backend for completion request");
    return;
  }

  const McpBackendConfig* backend = config_->findBackend(backend_name);
  if (!backend) {
    config_->stats().rq_unknown_backend_.inc();
    sendHttpError(400, fmt::format("Unknown backend '{}' in completion ref", backend_name));
    return;
  }

  auto start_completion = [this, backend]() {
    initializeSingleBackend(*backend, [weak_self = weak_from_this()](BackendResponse resp) {
      auto self = weak_self.lock();
      if (!self) {
        return;
      }
      if (resp.success) {
        self->sendJsonResponse(resp.body, self->encoded_session_id_);
      } else {
        self->config_->stats().rq_backend_failure_.inc();
        self->sendHttpError(500, resp.error.empty() ? "Backend request failed" : resp.error);
      }
    });

    if (lazy_init_request_body_.length() > 0) {
      if (needs_body_rewrite_) {
        config_->stats().rq_body_rewrite_.inc();
        rewriteCompletionCompleteBody(lazy_init_request_body_);
        needs_body_rewrite_ = false;
      }
      streamData(lazy_init_request_body_, true);
    }
  };

  if (config_->lazyInitialization() && !initialized_backends_.contains(backend_name)) {
    lazy_init_pending_ = true;
    lazyInitSingleBackend(*backend, [weak_self = weak_from_this(),
                                     start_completion = std::move(start_completion),
                                     backend_name](bool success) {
      auto self = weak_self.lock();
      if (!self) {
        return;
      }
      self->lazy_init_pending_ = false;
      if (!success) {
        self->config_->stats().rq_backend_failure_.inc();
        self->sendHttpError(500, fmt::format("Failed to initialize backend '{}'", backend_name));
        return;
      }
      start_completion();
    });
    return;
  }

  start_completion();
}

void McpRouterFilter::handleLoggingSetLevel() {
  ENVOY_LOG(debug, "logging/setLevel: fanout to {} backends", config_->backends().size());

  auto start_logging = [this]() {
    initializeFanout([weak_self = weak_from_this()](std::vector<BackendResponse> responses) {
      auto self = weak_self.lock();
      if (!self) {
        ENVOY_LOG(debug, "logging/setLevel callback ignored: filter destroyed");
        return;
      }
      bool any_success = false;
      for (const auto& resp : responses) {
        if (resp.success) {
          any_success = true;
          break;
        }
      }

      if (!any_success) {
        self->config_->stats().rq_fanout_failure_.inc();
        self->sendHttpError(500, "All backends failed to set logging level");
        return;
      }

      std::string response =
          fmt::format(R"({{"jsonrpc":"2.0","id":{},"result":{{}}}})", self->request_id_);
      self->sendJsonResponse(response, self->encoded_session_id_);
    });

    if (lazy_init_request_body_.length() > 0) {
      streamData(lazy_init_request_body_, true);
    }
  };

  if (config_->lazyInitialization() && initialized_backends_.size() < config_->backends().size()) {
    lazy_init_pending_ = true;
    lazyInitFanout(
        [weak_self = weak_from_this(), start_logging = std::move(start_logging)](bool success) {
          auto self = weak_self.lock();
          if (!self) {
            return;
          }
          self->lazy_init_pending_ = false;
          if (!success) {
            self->config_->stats().rq_fanout_failure_.inc();
            self->sendHttpError(500, "Failed to initialize backends");
            return;
          }
          start_logging();
        });
    return;
  }

  start_logging();
}

void McpRouterFilter::handleServerResponse() {
  auto [backend_name, original_id] = parseServerResponseId(server_response_id_);

  if (backend_name.empty()) {
    config_->stats().rq_unknown_backend_.inc();
    sendHttpError(400, fmt::format("Invalid server response ID '{}': cannot determine backend",
                                   server_response_id_));
    return;
  }

  const McpBackendConfig* backend = config_->findBackend(backend_name);
  if (!backend) {
    config_->stats().rq_unknown_backend_.inc();
    sendHttpError(400, fmt::format("Unknown backend '{}' in response ID", backend_name));
    return;
  }

  original_response_id_ = original_id;
  needs_body_rewrite_ = (server_response_id_ != original_response_id_);

  ENVOY_LOG(debug, "server_response: backend='{}', id='{}' -> '{}', needs_rewrite={}", backend_name,
            server_response_id_, original_id, needs_body_rewrite_);

  initializeSingleBackend(*backend, [weak_self = weak_from_this()](BackendResponse resp) {
    auto self = weak_self.lock();
    if (!self) {
      return;
    }
    if (resp.success) {
      self->sendJsonResponse(resp.body, self->encoded_session_id_);
    } else {
      self->config_->stats().rq_backend_failure_.inc();
      self->sendHttpError(500, resp.error.empty() ? "Backend request failed" : resp.error);
    }
  });
}

// Response aggregation helpers.

std::string McpRouterFilter::extractJsonRpcFromResponse(const BackendResponse& response) {
  const std::string& result = response.getJsonRpc();
  ENVOY_LOG(debug,
            "extractJsonRpcFromResponse: backend='{}', content_type={}, body_size={}, "
            "extracted_size={}",
            response.backend_name, static_cast<int>(response.content_type), response.body.size(),
            result.size());
  return result;
}

std::string McpRouterFilter::aggregateInitialize(const std::vector<BackendResponse>& responses) {
  // Empty responses is valid for lazy initialization (no backends contacted yet).
  if (!responses.empty()) {
    const bool any_success = std::any_of(responses.begin(), responses.end(),
                                         [](const BackendResponse& resp) { return resp.success; });

    if (!any_success) {
      config_->stats().rq_fanout_failure_.inc();
      return absl::StrCat(R"({"jsonrpc":"2.0","id":)", request_id_,
                          R"(,"error":{"code":-32603,"message":"All backends failed"}})");
    }
  }

  // Return gateway capabilities.
  return absl::StrCat(
      R"({"jsonrpc":"2.0","id":)", request_id_, R"(,"result":{)", R"("protocolVersion":")",
      kProtocolVersion, R"(",)",
      R"("capabilities":{"tools":{"listChanged":true},"prompts":{"listChanged":true},"resources":{"listChanged":true,"subscribe":true},"elicitation":{}},)",
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
    std::string json_body = extractJsonRpcFromResponse(resp);
    ENVOY_LOG(debug, "Aggregating tools from backend '{}': {}", resp.backend_name, json_body);
    extractAndPrefixTools(json_body, resp.backend_name, is_multiplexing, all_tools);
  }

  return absl::StrCat(R"({"jsonrpc":"2.0","id":)", request_id_, R"(,"result":{"tools":[)",
                      absl::StrJoin(all_tools, ","), "]}}");
}

// Shared aggregation for resources/list and resources/templates/list.
std::string
McpRouterFilter::aggregateResourceItems(const std::vector<BackendResponse>& responses,
                                        const std::string& result_key, const std::string& uri_field,
                                        const std::vector<std::string>& optional_fields) {
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
      result_map->addKey(result_key);
      {
        auto items_array = result_map->addArray();

        for (const auto& resp : responses) {
          if (!resp.success) {
            continue;
          }
          std::string json_body = extractJsonRpcFromResponse(resp);
          ENVOY_LOG(debug, "Aggregating {} from backend '{}': {}", result_key, resp.backend_name,
                    json_body);
          auto parsed_or = Json::Factory::loadFromString(json_body);
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

          auto items_or = (*result_or)->getObjectArray(result_key);
          if (!items_or.ok()) {
            continue;
          }

          for (const auto& item : *items_or) {
            if (!item || !item->isObject()) {
              continue;
            }

            auto uri_or = item->getString(uri_field);
            if (!uri_or.ok()) {
              continue;
            }

            auto item_map = items_array->addMap();

            // Prefix URI: "file://path" -> "backend+file://path".
            item_map->addKey(uri_field);
            if (is_multiplexing) {
              std::string original_uri = *uri_or;
              size_t scheme_end = original_uri.find("://");
              if (scheme_end != std::string::npos) {
                std::string scheme = original_uri.substr(0, scheme_end);
                std::string rest = original_uri.substr(scheme_end);
                item_map->addString(absl::StrCat(resp.backend_name, "+", scheme, rest));
              } else {
                item_map->addString(absl::StrCat(resp.backend_name, "+://", original_uri));
              }
            } else {
              item_map->addString(*uri_or);
            }

            for (const auto& field : optional_fields) {
              auto val_or = item->getString(field, "");
              if (val_or.ok() && !val_or->empty()) {
                item_map->addKey(field);
                item_map->addString(*val_or);
              }
            }
          }
        }
      }
    }
  }

  return output;
}

std::string McpRouterFilter::aggregateResourcesList(const std::vector<BackendResponse>& responses) {
  return aggregateResourceItems(responses, "resources", "uri", {"name", "description", "mimeType"});
}

std::string
McpRouterFilter::aggregateResourcesTemplatesList(const std::vector<BackendResponse>& responses) {
  return aggregateResourceItems(responses, "resourceTemplates", "uriTemplate",
                                {"name", "description", "title", "mimeType"});
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
          std::string json_body = extractJsonRpcFromResponse(resp);
          ENVOY_LOG(debug, "Aggregating prompts list from backend '{}': {}", resp.backend_name,
                    json_body);
          auto parsed_or = Json::Factory::loadFromString(json_body);
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

  // Set required headers for MCP backend.
  headers->setMethod(Http::Headers::get().MethodValues.Post);
  headers->setPath(backend.path);
  // Use host_rewrite_literal if configured, otherwise pass through original host.
  if (!backend.host_rewrite_literal.empty()) {
    headers->setHost(backend.host_rewrite_literal);
  } else if (request_headers_ != nullptr) {
    headers->setHost(request_headers_->getHostValue());
  }
  headers->setContentType(std::string(kContentTypeJson));

  // Accept both JSON and SSE responses.
  headers->addCopy(Http::LowerCaseString("accept"),
                   absl::StrCat(kContentTypeSse, ", ", kContentTypeJson));

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
        } else if (method_ == McpMethod::CompletionComplete) {
          // Completion ref rewriting: depends on ref type.
          if (completion_ref_type_ == "ref/prompt") {
            size_delta = static_cast<int64_t>(unprefixed_prompt_name_.size()) -
                         static_cast<int64_t>(prompt_name_.size());
          } else if (completion_ref_type_ == "ref/resource") {
            size_delta = static_cast<int64_t>(rewritten_uri_.size()) -
                         static_cast<int64_t>(resource_uri_.size());
          }
        } else if (method_ == McpMethod::ServerResponse) {
          // Server response ID rewriting: "time__42" -> 42 (or "42").
          std::string quoted_id = absl::StrCat("\"", server_response_id_, "\"");
          bool is_numeric = !original_response_id_.empty() &&
                            std::all_of(original_response_id_.begin(), original_response_id_.end(),
                                        [](char c) { return c >= '0' && c <= '9'; });
          std::string replacement =
              is_numeric ? original_response_id_ : absl::StrCat("\"", original_response_id_, "\"");
          size_delta =
              static_cast<int64_t>(replacement.size()) - static_cast<int64_t>(quoted_id.size());
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
  // If SSE headers were already sent (due to intermediate events), send response as SSE event.
  if (sse_headers_sent_) {
    ENVOY_LOG(debug, "Sending aggregated response as SSE event: {}", body);
    Buffer::OwnedImpl response_body;
    response_body.add("event: message\ndata: ");
    response_body.add(body);
    response_body.add("\n\n");
    decoder_callbacks_->encodeData(response_body, true);
    return;
  }

  // Standard JSON response path.
  auto headers = Http::ResponseHeaderMapImpl::create();
  headers->setStatus(200);
  headers->setContentType(std::string(kContentTypeJson));
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

std::string McpRouterFilter::buildSyntheticInitBody() {
  return absl::StrCat(R"({"jsonrpc":"2.0","id":)", request_id_,
                      R"(,"method":"initialize","params":{"protocolVersion":")", kProtocolVersion,
                      R"(","capabilities":{},"clientInfo":{"name":")", kGatewayName,
                      R"(","version":")", kGatewayVersion, R"("}}})");
}

void McpRouterFilter::resetStreamState() {
  if (multistream_) {
    multistream_.reset();
  }
  stream_callbacks_.clear();
  upstream_headers_.clear();
  aggregation_callback_ = nullptr;
  single_backend_callback_ = nullptr;
  pending_responses_.reset();
  response_count_.reset();
}

void McpRouterFilter::updateEncodedSessionId() {
  std::string composite =
      SessionCodec::buildCompositeSessionId(route_name_, session_subject_, backend_sessions_);
  encoded_session_id_ = SessionCodec::encode(composite);
}

void McpRouterFilter::lazyInitSingleBackend(const McpBackendConfig& backend,
                                            std::function<void(bool)> on_init_complete) {
  ENVOY_LOG(debug, "Lazy init: initializing backend '{}'", backend.name);

  std::string init_body = buildSyntheticInitBody();

  auto headers = createUpstreamHeaders(backend);
  headers->setContentLength(init_body.size());

  if (!muxdemux_->isIdle()) {
    ENVOY_LOG(warn, "MuxDemux not idle for lazy init of '{}'", backend.name);
    on_init_complete(false);
    return;
  }

  auto stream_cb = std::make_shared<BackendStreamCallbacks>(
      backend.name,
      [weak_self = weak_from_this(), backend_name = backend.name,
       on_init_complete = std::move(on_init_complete)](BackendResponse resp) {
        auto self = weak_self.lock();
        if (!self) {
          return;
        }

        if (!resp.success) {
          ENVOY_LOG(warn, "Lazy init failed for backend '{}': {}", backend_name, resp.error);
          on_init_complete(false);
          return;
        }

        if (!resp.session_id.empty()) {
          self->backend_sessions_[backend_name] = resp.session_id;
        }
        self->initialized_backends_.insert(backend_name);
        self->updateEncodedSessionId();

        ENVOY_LOG(debug, "Lazy init: backend '{}' initialized successfully", backend_name);

        self->resetStreamState();
        on_init_complete(true);
      },
      request_id_, false, weak_from_this());

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
    ENVOY_LOG(error, "Failed to start lazy init for '{}': {}", backend.name,
              multistream_or.status().message());
    on_init_complete(false);
    return;
  }

  multistream_ = std::move(*multistream_or);
  upstream_headers_.clear();
  upstream_headers_.push_back(std::move(headers));

  auto stream_it = multistream_->begin();
  if (stream_it != multistream_->end()) {
    (*stream_it)->sendHeaders(*upstream_headers_.back(), false);
    Buffer::OwnedImpl body(init_body);
    (*stream_it)->sendData(body, true);
  }
}

void McpRouterFilter::lazyInitFanout(std::function<void(bool)> on_init_complete) {
  std::vector<std::string> uninit_backends;
  for (const auto& backend : config_->backends()) {
    if (!initialized_backends_.contains(backend.name)) {
      uninit_backends.push_back(backend.name);
    }
  }

  if (uninit_backends.empty()) {
    on_init_complete(true);
    return;
  }

  ENVOY_LOG(debug, "Lazy init fanout: initializing {} backends", uninit_backends.size());
  initializeFanout(
      [weak_self = weak_from_this(),
       on_init_complete = std::move(on_init_complete)](std::vector<BackendResponse> responses) {
        auto self = weak_self.lock();
        if (!self) {
          return;
        }

        bool any_success = false;
        for (const auto& resp : responses) {
          if (resp.success) {
            any_success = true;
            if (!resp.session_id.empty()) {
              self->backend_sessions_[resp.backend_name] = resp.session_id;
            }
            self->initialized_backends_.insert(resp.backend_name);
          }
        }

        self->updateEncodedSessionId();
        self->resetStreamState();
        on_init_complete(any_success);
      },
      uninit_backends);
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
