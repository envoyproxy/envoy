#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "source/common/common/base64.h"
#include "source/common/http/headers.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/filters/http/mcp/mcp_filter.h"

#include "test/extensions/filters/http/common/fuzz/http_filter_fuzzer.h"
#include "test/extensions/filters/http/mcp/mcp_filter_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "nlohmann/json.hpp"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {
namespace {

namespace McpConstants = Filters::Common::Mcp::McpConstants;
using McpProto = envoy::extensions::filters::http::mcp::v3::Mcp;
using testing::_;
using testing::NiceMock;
using testing::Return;

struct LocalReply {
  Http::Code code;
  std::string body;
  std::string content_type;
};

std::optional<absl::string_view> firstHeaderValue(const Http::RequestHeaderMap& headers,
                                                  absl::string_view name) {
  const auto values = headers.get(Http::LowerCaseString(name));
  if (values.empty()) {
    return std::nullopt;
  }
  return values[0]->value().getStringView();
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

const nlohmann::json* findJsonPath(const nlohmann::json& json,
                                   const std::vector<absl::string_view>& path) {
  const nlohmann::json* value = &json;
  for (const absl::string_view key : path) {
    if (!value->is_object()) {
      return nullptr;
    }
    const auto it = value->find(key);
    if (it == value->end()) {
      return nullptr;
    }
    value = &*it;
  }
  return value;
}

std::optional<double> jsonRpcErrorCode(const LocalReply& reply) {
  if (reply.content_type != Http::Headers::get().ContentTypeValues.Json) {
    return std::nullopt;
  }
  const nlohmann::json json = nlohmann::json::parse(reply.body, nullptr, false);
  const nlohmann::json* code =
      findJsonPath(json, {McpConstants::ERROR_FIELD, McpConstants::ERROR_CODE_FIELD});
  // JSON local replies must be well-formed JSON-RPC error responses.
  FUZZ_ASSERT(json.is_object() && json.contains(McpConstants::ID_FIELD));
  FUZZ_ASSERT(json.value(McpConstants::JSONRPC_FIELD, "") == McpConstants::JSONRPC_VERSION);
  FUZZ_ASSERT(code != nullptr && code->is_number());
  return code->get<double>();
}

nlohmann::json parseUnambiguousJson(absl::string_view body) {
  std::vector<absl::flat_hash_set<std::string>> object_keys;
  bool ambiguous = false;
  nlohmann::json json = nlohmann::json::parse(
      body,
      [&](int, nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
        if (event == nlohmann::json::parse_event_t::object_start) {
          object_keys.emplace_back();
        } else if (event == nlohmann::json::parse_event_t::object_end) {
          object_keys.pop_back();
        } else if (event == nlohmann::json::parse_event_t::key) {
          const std::string& key = parsed.get_ref<const std::string&>();
          if (key.empty() || !object_keys.back().insert(key).second) {
            ambiguous = true;
          }
        }
        return true;
      },
      false);
  return ambiguous ? nlohmann::json(nlohmann::json::value_t::discarded) : json;
}

void verifyAcceptedNewSpecRequest(const McpFilterConfig& config,
                                  const Http::RequestHeaderMap& headers, absl::string_view body) {
  const nlohmann::json json = parseUnambiguousJson(body);
  const nlohmann::json* method = findJsonPath(json, {McpConstants::METHOD_FIELD});
  if (method == nullptr || !method->is_string()) {
    return;
  }
  const std::string& method_value = method->get_ref<const std::string&>();
  FUZZ_ASSERT(firstHeaderValue(headers, McpConstants::MCP_METHOD_HEADER) == method_value);

  const std::string name_path = config.parserConfig().getNameAttributePath(method_value);
  if (!name_path.empty()) {
    const nlohmann::json* name = findJsonPath(json, absl::StrSplit(name_path, '.'));
    const std::optional<absl::string_view> name_header =
        firstHeaderValue(headers, McpConstants::MCP_NAME_HEADER);
    FUZZ_ASSERT(name != nullptr && name->is_string() && name_header.has_value() &&
                decodeMcpHeaderValue(*name_header) == name->get_ref<const std::string&>());
  }

  const nlohmann::json* meta =
      findJsonPath(json, {McpConstants::PARAMS_FIELD, McpConstants::META_FIELD});
  const nlohmann::json* version =
      meta != nullptr ? findJsonPath(*meta, {McpConstants::MCP_META_PROTOCOL_VERSION_FIELD})
                      : nullptr;
  if (version != nullptr && version->is_string()) {
    FUZZ_ASSERT(version->get_ref<const std::string&>() == McpConstants::MCP_VERSION_2026_07_28);
  }

  const nlohmann::json* id = findJsonPath(json, {McpConstants::ID_FIELD});
  if (id != nullptr && (id->is_string() || id->is_number())) {
    FUZZ_ASSERT(meta != nullptr &&
                meta->contains(McpConstants::MCP_META_CLIENT_CAPABILITIES_FIELD));
  }
}

void verifyInvariants(const McpProto& proto_config, const McpFilterConfig& config,
                      const Http::RequestHeaderMap& headers, absl::string_view body,
                      const std::vector<LocalReply>& replies,
                      const envoy::config::core::v3::Metadata& metadata) {
  const bool reject_mode = proto_config.traffic_mode() == McpProto::REJECT_NO_MCP;
  const bool new_spec = firstHeaderValue(headers, McpConstants::MCP_PROTOCOL_VERSION_HEADER) ==
                        McpConstants::MCP_VERSION_2026_07_28;

  FUZZ_ASSERT(reject_mode || replies.empty());
  if (proto_config.traffic_mode() == McpProto::NOOP) {
    FUZZ_ASSERT(
        !metadata.filter_metadata().contains(std::string(McpConstants::McpfilterNamespace)));
  }

  for (const LocalReply& reply : replies) {
    FUZZ_ASSERT(reply.code == Http::Code::BadRequest || reply.code == Http::Code::MethodNotAllowed);
    const std::optional<double> error_code = jsonRpcErrorCode(reply);
    if (reply.code == Http::Code::MethodNotAllowed ||
        error_code == McpConstants::MCP_HEADER_MISMATCH_ERROR_CODE ||
        error_code == McpConstants::JSONRPC_INVALID_PARAMS_ERROR_CODE) {
      FUZZ_ASSERT(new_spec);
    }
    // Protocol versions are only rejected when a maximum supported version is configured.
    if (error_code == McpConstants::MCP_UNSUPPORTED_PROTOCOL_VERSION_ERROR_CODE) {
      FUZZ_ASSERT(proto_config.has_max_supported_protocol_version());
    }
  }

  if (reject_mode && new_spec && replies.empty()) {
    verifyAcceptedNewSpecRequest(config, headers, body);
  }
}

class McpFilterFuzzer : public HttpFilterFuzzer {
public:
  McpFilterFuzzer() {
    ON_CALL(callbacks_, requestHeaders())
        .WillByDefault(Return(makeOptRef<Http::RequestHeaderMap>(request_headers_)));
    ON_CALL(callbacks_, sendLocalReply(_, _, _, _, _))
        .WillByDefault([this](Http::Code code, absl::string_view body,
                              std::function<void(Http::ResponseHeaderMap&)> modify_headers,
                              const std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
          Http::TestResponseHeaderMapImpl headers;
          if (modify_headers != nullptr) {
            modify_headers(headers);
          }
          ENVOY_LOG_MISC(debug, "MCP filter local reply {}: {}", static_cast<int>(code), body);
          replies_.push_back({code, std::string(body), std::string(headers.getContentTypeValue())});
          enabled_ = false;
        });
  }

  void fuzz(const ::test::extensions::filters::http::mcp::McpFilterFuzzTestCase& input) {
    reset();
    replies_.clear();
    callbacks_.stream_info_.metadata_.Clear();
    callbacks_.stream_info_.filter_state_ = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::FilterChain);

    const auto config =
        std::make_shared<McpFilterConfig>(input.config(), "fuzz.", *stats_store_.rootScope());
    McpFilter filter(config);
    filter.setDecoderFilterCallbacks(callbacks_);
    runData(static_cast<Http::StreamDecoderFilter*>(&filter), input.request());
    filter.onDestroy();

    verifyInvariants(input.config(), *config, request_headers_,
                     absl::StrJoin(Fuzz::parseHttpData(input.request()), ""), replies_,
                     callbacks_.stream_info_.metadata_);
  }

private:
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  std::vector<LocalReply> replies_;
};

DEFINE_PROTO_FUZZER(const ::test::extensions::filters::http::mcp::McpFilterFuzzTestCase& input) {
  try {
    TestUtility::validate(input);
  } catch (const EnvoyException& e) {
    ENVOY_LOG_MISC(debug, "EnvoyException during validation: {}", e.what());
    return;
  }

  static McpFilterFuzzer fuzzer;
  fuzzer.fuzz(input);
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
