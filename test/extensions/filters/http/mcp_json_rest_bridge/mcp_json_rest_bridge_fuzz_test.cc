#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "envoy/extensions/filters/http/mcp_json_rest_bridge/v3/mcp_json_rest_bridge.pb.h"

#include "source/common/common/base64.h"
#include "source/common/http/utility.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/bridge_status.h"
#include "source/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_filter.h"

#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/mcp_json_rest_bridge/mcp_json_rest_bridge_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "absl/algorithm/container.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_join.h"
#include "nlohmann/json.hpp"
#include "re2/re2.h"
#include "utf8_validity.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpJsonRestBridge {
namespace {

namespace McpConstants = Filters::Common::Mcp::McpConstants;
using ::envoy::extensions::filters::http::mcp_json_rest_bridge::v3::HttpRule;
using ::envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpJsonRestBridge;
using ::envoy::extensions::filters::http::mcp_json_rest_bridge::v3::McpParamHeader;
using ::nlohmann::json;
using ::test::extensions::filters::http::mcp_json_rest_bridge::McpJsonRestBridgeFuzzTestCase;
using testing::_;
using testing::NiceMock;
using testing::Return;

constexpr int64_t JsonRpcMethodNotFound = -32601;
// MCP integer values are within +/-(2^53 - 1).
constexpr double MaxSafeInteger = 9007199254740991.0;

std::vector<absl::string_view> headerValues(const Http::HeaderMap& headers,
                                            absl::string_view name) {
  std::vector<absl::string_view> values;
  const auto result = headers.get(Http::LowerCaseString(name));
  for (size_t i = 0; i < result.size(); ++i) {
    values.push_back(result[i]->value().getStringView());
  }
  return values;
}

const json* findJsonPath(const json& value, const std::vector<absl::string_view>& path) {
  const json* current = &value;
  for (const absl::string_view key : path) {
    if (!current->is_object()) {
      return nullptr;
    }
    const auto it = current->find(key);
    if (it == current->end()) {
      return nullptr;
    }
    current = &*it;
  }
  return current;
}

std::optional<std::string> decodeMcpHeaderValue(absl::string_view value) {
  const absl::string_view prefix = McpConstants::MCP_BASE64_PREFIX;
  const absl::string_view suffix = McpConstants::MCP_BASE64_SUFFIX;
  if (value.size() < prefix.size() + suffix.size() || !absl::StartsWith(value, prefix) ||
      !absl::EndsWith(value, suffix)) {
    return std::string(value);
  }
  const absl::string_view payload =
      value.substr(prefix.size(), value.size() - prefix.size() - suffix.size());
  std::string decoded = Base64::decode(payload);
  if (decoded.empty() && !payload.empty()) {
    return std::nullopt;
  }
  return decoded;
}

bool mcpParamMatches(McpParamHeader::Type type, const json& argument, absl::string_view value) {
  switch (type) {
  case McpParamHeader::STRING:
    return argument.is_string() && argument.get_ref<const std::string&>() == value;
  case McpParamHeader::BOOLEAN:
    return argument.is_boolean() && value == (argument.get<bool>() ? "true" : "false");
  case McpParamHeader::INTEGER: {
    double number = 0;
    return RE2::FullMatch(value, R"(-?[0-9]+(\.0+)?)") && absl::SimpleAtod(value, &number) &&
           std::abs(number) <= MaxSafeInteger && argument.is_number() &&
           argument.get<double>() == number;
  }
  default:
    return false;
  }
}

void verifyAcceptedStatelessRequest(const json& request, const Http::RequestHeaderMap& headers,
                                    const McpJsonRestBridgeFilterConfig& config,
                                    absl::string_view host, absl::string_view path) {
  const json* method = findJsonPath(request, {McpConstants::METHOD_FIELD});
  FUZZ_ASSERT(method != nullptr && method->is_string());
  const std::string& method_value = method->get_ref<const std::string&>();
  FUZZ_ASSERT(method_value == McpConstants::Methods::TOOLS_LIST ||
              method_value == McpConstants::Methods::TOOLS_CALL ||
              method_value == McpConstants::Methods::SERVER_DISCOVER);
  FUZZ_ASSERT(headerValues(headers, McpConstants::MCP_METHOD_HEADER) ==
              std::vector<absl::string_view>{method_value});
  const json* capabilities =
      findJsonPath(request, {McpConstants::PARAMS_FIELD, McpConstants::META_FIELD,
                             McpConstants::MCP_META_CLIENT_CAPABILITIES_FIELD});
  FUZZ_ASSERT(capabilities != nullptr && capabilities->is_object());
  if (method_value != McpConstants::Methods::TOOLS_CALL) {
    return;
  }

  const json* name = findJsonPath(request, {McpConstants::PARAMS_FIELD, McpConstants::NAME_FIELD});
  const std::vector<absl::string_view> name_headers =
      headerValues(headers, McpConstants::MCP_NAME_HEADER);
  FUZZ_ASSERT(name != nullptr && name->is_string() && name_headers.size() == 1 &&
              decodeMcpHeaderValue(name_headers[0]) == name->get_ref<const std::string&>());

  const json* arguments =
      findJsonPath(request, {McpConstants::PARAMS_FIELD, McpConstants::ARGUMENTS_FIELD});
  for (const McpParamHeaderMapping& mapping :
       config.mcpParamHeaders(name->get_ref<const std::string&>(), host, path)) {
    const json* argument =
        arguments == nullptr
            ? nullptr
            : findJsonPath(*arguments, std::vector<absl::string_view>(mapping.property_path.begin(),
                                                                      mapping.property_path.end()));
    const std::vector<absl::string_view> values = headerValues(headers, mapping.header_name.get());
    // The header must be absent when the argument is absent or null.
    if (argument == nullptr || argument->is_null()) {
      FUZZ_ASSERT(values.empty());
      continue;
    }
    // Otherwise it must be present once, with only visible ASCII, space or tab before decoding.
    FUZZ_ASSERT(values.size() == 1 && absl::c_all_of(values[0], [](char c) {
                  return c == '\t' || (c >= ' ' && c <= '~');
                }));
    const std::optional<std::string> value = decodeMcpHeaderValue(values[0]);
    FUZZ_ASSERT(value.has_value() && mcpParamMatches(mapping.type, *argument, *value));
  }
}

struct LocalReply {
  Http::TestResponseHeaderMapImpl headers;
  std::string body;
};

// Replaces the characters that header values cannot contain in the paths of an HTTP rule.
void sanitizePaths(HttpRule& rule) {
  for (std::string* path : {rule.mutable_get(), rule.mutable_put(), rule.mutable_post(),
                            rule.mutable_delete_(), rule.mutable_patch()}) {
    *path = Fuzz::replaceInvalidCharacters(*path);
  }
}

// The result of one fuzz input, as seen by the client.
struct Outcome {
  // The bridge's dynamic metadata after the request was decoded.
  Protobuf::Struct decode_metadata;
  bool decode_local_reply{};
  bool encode_local_reply{};
  uint64_t status{};
  std::string body;
  uint64_t upstream_status{};
  bool upstream_body_is_utf8{true};
};

void verifyInvariants(const McpJsonRestBridge& proto_config,
                      const McpJsonRestBridgeFilterConfig& config,
                      const Http::RequestHeaderMap& headers, const json& request,
                      const Outcome& outcome) {
  const auto& fields = outcome.decode_metadata.fields();
  const auto status = fields.find(std::string(BridgeStatusValues::STATUS));
  // Requests that do not reach the bridge's JSON-RPC handling are passed through unchanged.
  if (status == fields.end()) {
    return;
  }
  const std::string& decode_status = status->second.string_value();
  // The bridge does not produce the responses to non-POST requests and passed-through tools/list.
  if (decode_status == BridgeStatusValues::HTTP_REQUEST_METHOD_NOT_POST ||
      decode_status == BridgeStatusValues::INTERNAL_TOOLS_LIST_PASSTHROUGH) {
    return;
  }
  const auto method_field = fields.find(std::string(McpConstants::METHOD_FIELD));
  const std::string method =
      method_field == fields.end() ? "" : method_field->second.string_value();
  const bool accepted = decode_status == BridgeStatusValues::OK;
  const bool legacy_only = proto_config.server_info().max_supported_protocol_version().value() !=
                           McpConstants::MCP_VERSION_2026_07_28;
  const json* meta_version =
      findJsonPath(request, {McpConstants::PARAMS_FIELD, McpConstants::META_FIELD,
                             McpConstants::MCP_META_PROTOCOL_VERSION_FIELD});
  const bool stateless =
      !legacy_only &&
      headerValues(headers, McpConstants::MCP_PROTOCOL_VERSION_HEADER) ==
          std::vector<absl::string_view>{McpConstants::MCP_VERSION_2026_07_28} &&
      meta_version != nullptr && meta_version->is_string() &&
      meta_version->get_ref<const std::string&>() == McpConstants::MCP_VERSION_2026_07_28;
  // The bridge does not parse request bodies that exceed the size limit.
  const json* id = decode_status == BridgeStatusValues::REQUEST_TOO_LARGE
                       ? nullptr
                       : findJsonPath(request, {McpConstants::ID_FIELD});
  const bool has_id = id != nullptr && (id->is_number_integer() || id->is_string());

  const bool from_upstream = !outcome.decode_local_reply && !outcome.encode_local_reply;
  const json response = json::parse(outcome.body, nullptr, false);
  const json* result = findJsonPath(response, {McpConstants::RESULT_FIELD});
  // Results generated by the bridge, as opposed to copied from an upstream tools/list response.
  const bool bridge_result = result != nullptr && result->is_object() &&
                             (!from_upstream || method == McpConstants::Methods::TOOLS_CALL);
  const json* error_code =
      findJsonPath(response, {McpConstants::ERROR_FIELD, McpConstants::ERROR_CODE_FIELD});
  const int64_t code =
      error_code != nullptr && error_code->is_number_integer() ? error_code->get<int64_t>() : 0;

  if (!outcome.body.empty() && (!from_upstream || outcome.upstream_body_is_utf8)) {
    FUZZ_ASSERT(response.is_object() &&
                response.value(McpConstants::JSONRPC_FIELD, "") == McpConstants::JSONRPC_VERSION);
    FUZZ_ASSERT(response.contains(McpConstants::RESULT_FIELD) !=
                response.contains(McpConstants::ERROR_FIELD));
    FUZZ_ASSERT(response.contains(McpConstants::ID_FIELD) &&
                response.at(McpConstants::ID_FIELD) == (has_id ? *id : json(nullptr)));
  }

  // Without 2026-07-28 support, responses keep the legacy behavior.
  if (legacy_only) {
    FUZZ_ASSERT(outcome.status != 404 && code != McpConstants::MCP_HEADER_MISMATCH_ERROR_CODE);
    FUZZ_ASSERT(!bridge_result || (!result->contains(McpConstants::RESULT_TYPE_FIELD) &&
                                   !result->contains(McpConstants::TTL_MS_FIELD) &&
                                   !result->contains(McpConstants::CACHE_SCOPE_FIELD)));
  }

  if (stateless && has_id && accepted) {
    // The bridge selects the endpoint by host and by the path without the query.
    const absl::string_view host = Http::Utility::parseAuthority(headers.getHostValue()).host_;
    absl::string_view path = headers.getPathValue();
    path = path.substr(0, path.find('?'));
    verifyAcceptedStatelessRequest(request, headers, config, host, path);
    FUZZ_ASSERT(!bridge_result || result->value(McpConstants::RESULT_TYPE_FIELD, "") ==
                                      McpConstants::RESULT_TYPE_COMPLETE);
  }

  // HTTP status codes of JSON-RPC errors and of transformed upstream responses.
  if (!outcome.encode_local_reply) {
    if (code == McpConstants::MCP_HEADER_MISMATCH_ERROR_CODE) {
      FUZZ_ASSERT(outcome.status == 400);
    }
    if (code == JsonRpcMethodNotFound && has_id && (stateless || legacy_only)) {
      FUZZ_ASSERT(outcome.status == (stateless ? 404 : 200));
    }
    if (accepted && from_upstream &&
        (method == McpConstants::Methods::TOOLS_CALL ||
         method == McpConstants::Methods::TOOLS_LIST)) {
      const uint64_t upstream = outcome.upstream_status;
      FUZZ_ASSERT(outcome.status == (upstream == 401 || upstream == 403 ? upstream : 200));
    }
  }
}

class McpJsonRestBridgeFilterFuzzer : public HttpFilterFuzzer {
public:
  McpJsonRestBridgeFilterFuzzer() {
    ON_CALL(decoder_callbacks_, requestHeaders())
        .WillByDefault(Return(makeOptRef<Http::RequestHeaderMap>(request_headers_)));
    ON_CALL(encoder_callbacks_, responseHeaders())
        .WillByDefault(Return(makeOptRef<Http::ResponseHeaderMap>(response_headers_)));
    ON_CALL(decoder_callbacks_, filterConfigName()).WillByDefault(Return(FilterName));
    const auto send_local_reply = [this](Http::Code code, absl::string_view body,
                                         std::function<void(Http::ResponseHeaderMap&)> modify,
                                         const std::optional<Grpc::Status::GrpcStatus>,
                                         absl::string_view) {
      LocalReply reply{
          Http::TestResponseHeaderMapImpl{{":status", std::to_string(static_cast<uint64_t>(code))}},
          std::string(body)};
      if (modify != nullptr) {
        modify(reply.headers);
      }
      (encoding_ ? encode_local_reply_ : decode_local_reply_) = std::move(reply);
      enabled_ = false;
    };
    ON_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _)).WillByDefault(send_local_reply);
    ON_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _)).WillByDefault(send_local_reply);
    ON_CALL(encoder_callbacks_, addEncodedData(_, _))
        .WillByDefault([this](Buffer::Instance& data, bool) { response_body_ += data.toString(); });
  }

  void fuzz(const McpJsonRestBridgeFuzzTestCase& input) {
    reset();
    encoding_ = false;
    decode_local_reply_.reset();
    encode_local_reply_.reset();
    response_body_.clear();
    decoder_callbacks_.stream_info_.metadata_.Clear();

    McpJsonRestBridge proto_config = input.config();
    proto_config.set_request_storage_mode(McpJsonRestBridge::DYNAMIC_METADATA);
    auto& tool_config = *proto_config.mutable_tool_config();
    if (tool_config.has_tool_list_http_rule()) {
      sanitizePaths(*tool_config.mutable_tool_list_http_rule());
    }
    for (auto& tool : *tool_config.mutable_tools()) {
      const std::string& input_schema = tool.tool_list_config().input_schema();
      // json::accept() stops at a NUL character, but the bridge copies the whole schema.
      if (input_schema.find('\0') != std::string::npos || !json::accept(input_schema)) {
        tool.mutable_tool_list_config()->clear_input_schema();
      }
      if (tool.has_http_rule()) {
        sanitizePaths(*tool.mutable_http_rule());
      }
    }
    const absl::StatusOr<McpJsonRestBridgeFilterConfigSharedPtr> config =
        McpJsonRestBridgeFilterConfig::create(proto_config);
    if (!config.ok()) {
      ENVOY_LOG_MISC(debug, "Invalid config: {}", config.status());
      return;
    }
    McpJsonRestBridgeFilter filter(*config);
    filter.setDecoderFilterCallbacks(decoder_callbacks_);
    filter.setEncoderFilterCallbacks(encoder_callbacks_);

    runData(static_cast<Http::StreamDecoderFilter*>(&filter), input.request());
    Outcome outcome;
    const auto metadata =
        decoder_callbacks_.stream_info_.metadata_.filter_metadata().find(FilterName);
    if (metadata != decoder_callbacks_.stream_info_.metadata_.filter_metadata().end()) {
      outcome.decode_metadata = metadata->second;
    }
    // Like HttpFilterFuzzer::runData(), the request headers default to a GET of /foo on foo.com.
    Http::TestRequestHeaderMapImpl headers =
        Fuzz::fromHeaders<Http::TestRequestHeaderMapImpl>(input.request().headers());
    if (headers.Path() == nullptr || headers.getPathValue().empty()) {
      headers.setPath("/foo");
    }
    if (headers.Host() == nullptr) {
      headers.setHost("foo.com");
    }

    encoding_ = true;
    if (decode_local_reply_.has_value()) {
      // The connection manager sends local replies through the encoder filters, where the bridge
      // turns its error replies into JSON-RPC responses.
      outcome.decode_local_reply = true;
      response_headers_ = decode_local_reply_->headers;
      encode(filter,
             decode_local_reply_->body.empty()
                 ? std::vector<std::string>{}
                 : std::vector<std::string>{decode_local_reply_->body},
             nullptr);
    } else {
      const test::fuzz::HttpData& upstream = input.upstream_response();
      response_headers_ = Fuzz::fromHeaders<Http::TestResponseHeaderMapImpl>(upstream.headers());
      if (!Http::Utility::getResponseStatusOrNullopt(response_headers_).has_value()) {
        response_headers_.setStatus(200);
      }
      outcome.upstream_status = Http::Utility::getResponseStatus(response_headers_);
      const std::vector<std::string> chunks = Fuzz::parseHttpData(upstream);
      outcome.upstream_body_is_utf8 = utf8_range::IsStructurallyValid(absl::StrJoin(chunks, ""));
      encode(filter, chunks, upstream.has_trailers() ? &upstream.trailers() : nullptr);
    }
    filter.onDestroy();

    outcome.encode_local_reply = encode_local_reply_.has_value();
    const Http::ResponseHeaderMap& final_headers =
        outcome.encode_local_reply ? encode_local_reply_->headers : response_headers_;
    outcome.status = Http::Utility::getResponseStatus(final_headers);
    outcome.body = outcome.encode_local_reply ? encode_local_reply_->body : response_body_;
    ENVOY_LOG_MISC(debug, "Bridge outcome: {} -> {} {}", outcome.decode_metadata.ShortDebugString(),
                   outcome.status, outcome.body);

    const json request =
        json::parse(absl::StrJoin(Fuzz::parseHttpData(input.request()), ""), nullptr, false);
    verifyInvariants(proto_config, **config, headers, request, outcome);
  }

private:
  // Encodes a response like HttpFilterFuzzer::runData(), which does not expose the encoded body.
  void encode(McpJsonRestBridgeFilter& filter, const std::vector<std::string>& chunks,
              const test::fuzz::Headers* trailers) {
    filter.encodeHeaders(response_headers_, chunks.empty() && trailers == nullptr);
    for (size_t i = 0; i < chunks.size() && !encode_local_reply_.has_value(); ++i) {
      Buffer::OwnedImpl data(chunks[i]);
      filter.encodeData(data, trailers == nullptr && i + 1 == chunks.size());
      response_body_ += data.toString();
    }
    if (trailers != nullptr && !encode_local_reply_.has_value()) {
      response_trailers_ = Fuzz::fromHeaders<Http::TestResponseTrailerMapImpl>(*trailers);
      filter.encodeTrailers(response_trailers_);
    }
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  bool encoding_{};
  std::optional<LocalReply> decode_local_reply_;
  std::optional<LocalReply> encode_local_reply_;
  std::string response_body_;
};

DEFINE_PROTO_FUZZER(const McpJsonRestBridgeFuzzTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  static McpJsonRestBridgeFilterFuzzer fuzzer;
  fuzzer.fuzz(input);
}

} // namespace
} // namespace McpJsonRestBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
