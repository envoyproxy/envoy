#include "source/extensions/http/mcp_handler/mcp_to_grpc/mcp_to_grpc.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/http/headers.h"
#include "source/common/grpc/common.h"
#include <nlohmann/json.hpp>
#include <regex>
#include "grpc_transcoding/json_request_translator.h"
#include "grpc_transcoding/path_matcher_utility.h"
#include "grpc_transcoding/response_to_json_translator.h"
using Envoy::Protobuf::FileDescriptorSet;
using Envoy::Protobuf::io::ZeroCopyInputStream;
using google::api::HttpRule;
using google::grpc::transcoding::JsonRequestTranslator;
using JsonRequestTranslatorPtr = std::unique_ptr<JsonRequestTranslator>;
using google::grpc::transcoding::MessageStream;
using google::grpc::transcoding::PathMatcherBuilder;
using google::grpc::transcoding::PathMatcherUtility;
using google::grpc::transcoding::RequestMessageTranslator;
using RequestMessageTranslatorPtr = std::unique_ptr<RequestMessageTranslator>;
using google::grpc::transcoding::ResponseToJsonTranslator;
using ResponseToJsonTranslatorPtr = std::unique_ptr<ResponseToJsonTranslator>;
using google::grpc::transcoding::Transcoder;
using TranscoderPtr = std::unique_ptr<Transcoder>;
using google::grpc::transcoding::TranscoderInputStream;
using TranscoderInputStreamPtr = std::unique_ptr<TranscoderInputStream>;
namespace Envoy {
namespace Extensions {
namespace Http {
namespace McpHandler {
namespace McpToGrpc {
constexpr absl::string_view JsonTypeNumber = "number";
constexpr absl::string_view JsonTypeInteger = "integer";
constexpr absl::string_view JsonTypeBoolean = "boolean";
constexpr absl::string_view JsonTypeString = "string";
constexpr absl::string_view JsonTypeObject = "object";
constexpr absl::string_view InputSchemaType = "type";
constexpr absl::string_view InputSchemaProperties = "properties";
constexpr absl::string_view InputSchemaRequired = "required";
constexpr absl::string_view InputSchemaDescription = "description";
constexpr absl::string_view OutputSchemaType = "type";
constexpr absl::string_view OutputSchemaProperties = "properties";
constexpr absl::string_view OutputSchemaRequired = "required";
constexpr absl::string_view OutputSchemaDescription = "description";
constexpr absl::string_view ResultNextCursor = "nextCursor";
constexpr absl::string_view ResultTools = "tools";
constexpr absl::string_view ToolName = "name";
constexpr absl::string_view ToolDescription = "description";
constexpr absl::string_view ToolInputSchema = "inputSchema";
constexpr absl::string_view ToolOutputSchema = "outputSchema";
constexpr absl::string_view ToolsCallArguments = "arguments";
namespace {
// Transcoder:
// https://github.com/grpc-ecosystem/grpc-httpjson-transcoding/blob/master/src/include/grpc_transcoding/transcoder.h
// implementation based on JsonRequestTranslator & ResponseToJsonTranslator
class TranscoderImpl : public Transcoder {
public:
  /**
   * Construct a transcoder implementation
   * @param request_translator a JsonRequestTranslator that does the request translation
   * @param response_translator a ResponseToJsonTranslator that does the response translation
   */
  TranscoderImpl(RequestMessageTranslatorPtr request_translator,
                 JsonRequestTranslatorPtr json_request_translator,
                 ResponseToJsonTranslatorPtr response_translator)
      : request_translator_(std::move(request_translator)),
        json_request_translator_(std::move(json_request_translator)),
        request_message_stream_(request_translator_ ? *request_translator_
                                                    : json_request_translator_->Output()),
        response_translator_(std::move(response_translator)),
        request_stream_(request_message_stream_.CreateInputStream()),
        response_stream_(response_translator_->CreateInputStream()) {}
  // Transcoder
  ::google::grpc::transcoding::TranscoderInputStream* RequestOutput() override {
    return request_stream_.get();
  }
  absl::Status RequestStatus() override { return request_message_stream_.Status(); }
  ZeroCopyInputStream* ResponseOutput() override { return response_stream_.get(); }
  absl::Status ResponseStatus() override { return response_translator_->Status(); }
private:
  RequestMessageTranslatorPtr request_translator_;
  JsonRequestTranslatorPtr json_request_translator_;
  MessageStream& request_message_stream_;
  ResponseToJsonTranslatorPtr response_translator_;
  TranscoderInputStreamPtr request_stream_;
  TranscoderInputStreamPtr response_stream_;
};
} // namespace
// Helper function parse comment and check if field is required
std::string isFieldRequiredAndCleanComment(const std::string& comment, bool& is_required) {
  if (comment.empty()) {
    return comment;
  }
  // Remove [REQUIRED] marker from comment
  std::regex required_regex(R"(\s*\[REQUIRED\]\s*)", std::regex_constants::icase);
  is_required = std::regex_search(comment, required_regex);
  return std::regex_replace(comment, required_regex, "");
}
std::string protoTypeToJsonType(Protobuf::FieldDescriptor::Type type) {
  switch (type) {
  case Protobuf::FieldDescriptor::TYPE_DOUBLE:
  case Protobuf::FieldDescriptor::TYPE_FLOAT:
    return JsonTypeNumber.data();
  case Protobuf::FieldDescriptor::TYPE_INT64:
  case Protobuf::FieldDescriptor::TYPE_UINT64:
  case Protobuf::FieldDescriptor::TYPE_INT32:
  case Protobuf::FieldDescriptor::TYPE_UINT32:
  case Protobuf::FieldDescriptor::TYPE_FIXED64:
  case Protobuf::FieldDescriptor::TYPE_FIXED32:
  case Protobuf::FieldDescriptor::TYPE_SFIXED32:
  case Protobuf::FieldDescriptor::TYPE_SFIXED64:
  case Protobuf::FieldDescriptor::TYPE_SINT32:
  case Protobuf::FieldDescriptor::TYPE_SINT64:
    return JsonTypeInteger.data();
  case Protobuf::FieldDescriptor::TYPE_BOOL:
    return JsonTypeBoolean.data();
  case Protobuf::FieldDescriptor::TYPE_STRING:
    return JsonTypeString.data();
  case Protobuf::FieldDescriptor::TYPE_BYTES:
    return JsonTypeString.data();
  case Protobuf::FieldDescriptor::TYPE_ENUM:
    return JsonTypeString.data();
  case Protobuf::FieldDescriptor::TYPE_MESSAGE:
    return JsonTypeObject.data();
  default:
    return JsonTypeString.data();
  }
}
McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl::McpToGrpcMcpHandlerImpl(
    const envoy::extensions::http::mcp_handler::mcp_to_grpc::v3::McpToGrpc& proto_config,
    Server::Configuration::CommonFactoryContext& context) {
  if (proto_config.services().empty()) {
    ENVOY_LOG(error, "McpToGrpcJson: No services found in config");
    return;
  }
  include_output_schema_ = proto_config.include_output_schema();
  Protobuf::FileDescriptorSet descriptor_set;
  switch (proto_config.descriptor_set_case()) {
  case envoy::extensions::http::mcp_handler::mcp_to_grpc::v3::McpToGrpc::DescriptorSetCase::
      kProtoDescriptor:
    if (!descriptor_set.ParseFromString(
            context.api().fileSystem().fileReadToEnd(proto_config.proto_descriptor()))) {
      ENVOY_LOG(error, "McpToGrpcJson: Unable to parse proto descriptor");
      return;
    }
    break;
  case envoy::extensions::http::mcp_handler::mcp_to_grpc::v3::McpToGrpc::DescriptorSetCase::
      kProtoDescriptorBin:
    if (!descriptor_set.ParseFromString(proto_config.proto_descriptor_bin())) {
      ENVOY_LOG(error, "McpToGrpcJson: Unable to parse proto descriptor");
      return;
    }
    break;
  case envoy::extensions::http::mcp_handler::mcp_to_grpc::v3::McpToGrpc::DescriptorSetCase::
      DESCRIPTOR_SET_NOT_SET:
    ENVOY_LOG(error, "McpToGrpcJson: descriptor not set");
    return;
  }
  for (const auto& file : descriptor_set.file()) {
    addFileDescriptor(file);
  }
  // TODO: add convert_grpc_status feature
  /*
  if (proto_config.convert_grpc_status()) {
    addBuiltinSymbolDescriptor("google.protobuf.Any");
    addBuiltinSymbolDescriptor("google.rpc.Status");
  }
  */
  type_helper_ = std::make_unique<google::grpc::transcoding::TypeHelper>(
      Protobuf::util::NewTypeResolverForDescriptorPool(Grpc::Common::typeUrlPrefix(),
                                                       &descriptor_pool_));
  PathMatcherBuilder<MethodInfoSharedPtr> pmb;
  for (const auto& service_name : proto_config.services()) {
    auto service = descriptor_pool_.FindServiceByName(service_name);
    if (service == nullptr) {
      ENVOY_LOG(error,
                "McpToGrpcJson: Could not find '" + service_name + "' in the proto descriptor");
      continue;
    }
    for (int i = 0; i < service->method_count(); ++i) {
      auto method = service->method(i);
      // Register method to grpc transcoder
      // Build http rule
      HttpRule http_rule;
      auto post = "/" + service->full_name() + "/" + method->name();
      http_rule.set_post(post);
      http_rule.set_body("*");
      // Create method info
      MethodInfoSharedPtr method_info;
      absl::Status status = createMethodInfo(method, http_rule, method_info);
      if (!status.ok()) {
        ENVOY_LOG(error, "McpToGrpc: Cannot register '", method->full_name(),
                  "': ", status.message());
        continue;
      }
      // Register to path matcher
      if (!PathMatcherUtility::RegisterByHttpRule(pmb, http_rule, {}, method_info)) {
        ENVOY_LOG(error, "McpToGrpc: Cannot register '", method->full_name(), "' to path matcher");
        continue;
      }
      // Register to tool registry
      //  Get method comments
      google::protobuf::SourceLocation method_loc;
      std::string method_comment;
      if (method->GetSourceLocation(&method_loc)) {
        method_comment = method_loc.leading_comments;
      }
      // Build inputSchema (JSON Schema)
      nlohmann::json input_schema;
      input_schema[InputSchemaType] = JsonTypeObject;
      input_schema[InputSchemaProperties] = nlohmann::json::object();
      input_schema[InputSchemaRequired] = nlohmann::json::array();
      const auto* input_desc = method->input_type();
      if (input_desc) {
        for (int fi = 0; fi < input_desc->field_count(); ++fi) {
          const auto* field_desc = input_desc->field(fi);
          google::protobuf::SourceLocation field_loc;
          std::string field_comment;
          if (field_desc->GetSourceLocation(&field_loc)) {
            field_comment = field_loc.leading_comments;
          }
          nlohmann::json prop;
          prop[InputSchemaType] = protoTypeToJsonType(field_desc->type());
          bool is_required = field_desc->is_required();
          // Clean comment and check if field is required
          std::string clean_comment = isFieldRequiredAndCleanComment(field_comment, is_required);
          if (!clean_comment.empty()) {
            prop[InputSchemaDescription] = clean_comment;
          }
          // Check if field is required (either by proto2 syntax or comment marker)
          if (is_required) {
            input_schema[InputSchemaRequired].push_back(field_desc->name());
          }
          input_schema[InputSchemaProperties][field_desc->name()] = prop;
        }
      }
      nlohmann::json output_schema;
      if (include_output_schema_) {
        // Build outputSchema (JSON Schema)
        output_schema[OutputSchemaType] = JsonTypeObject;
        output_schema[OutputSchemaProperties] = nlohmann::json::object();
        const auto* output_desc = method->output_type();
        if (output_desc) {
          for (int fi = 0; fi < output_desc->field_count(); ++fi) {
            const auto* field_desc = output_desc->field(fi);
            google::protobuf::SourceLocation field_loc;
            std::string field_comment;
            if (field_desc->GetSourceLocation(&field_loc)) {
              field_comment = field_loc.leading_comments;
            }
            nlohmann::json prop;
            prop[OutputSchemaType] = protoTypeToJsonType(field_desc->type());
            bool is_required = field_desc->is_required();
            // Clean comment and check if field is required
            std::string clean_comment = isFieldRequiredAndCleanComment(field_comment, is_required);
            if (!clean_comment.empty()) {
              prop[OutputSchemaDescription] = clean_comment;
            }
            if (is_required) {
              output_schema[OutputSchemaRequired].push_back(field_desc->name());
            }
            output_schema[OutputSchemaProperties][field_desc->name()] = prop;
          }
        }
      }
      // Fill ToolInfo
      ToolInfo tool;
      tool.tool_name = service->name() + "." + method->name();
      tool.description = method_comment;
      tool.input_schema = input_schema;
      tool.output_schema = output_schema;
      tool.grpc_path = "/" + service->full_name() + "/" + method->name();
      tool_registry_[tool.tool_name] = tool;
      ENVOY_LOG(error, "[MCP TOOL] method: {}.{} | description: {}", service->name(),
                method->name(), method_comment);
    }
  }
  path_matcher_ = pmb.Build();
  const auto& print_config = proto_config.print_options();
  response_translate_options_.json_print_options.add_whitespace = print_config.add_whitespace();
  response_translate_options_.json_print_options.always_print_primitive_fields =
      print_config.always_print_primitive_fields();
  response_translate_options_.json_print_options.always_print_enums_as_ints =
      print_config.always_print_enums_as_ints();
  response_translate_options_.json_print_options.preserve_proto_field_names =
      print_config.preserve_proto_field_names();
  response_translate_options_.stream_newline_delimited = print_config.stream_newline_delimited();
  case_insensitive_enum_parsing_ = proto_config.case_insensitive_enum_parsing();
}
void McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl::addFileDescriptor(
    const Protobuf::FileDescriptorProto& file) {
  if (descriptor_pool_.BuildFile(file) == nullptr) {
    ENVOY_LOG(error, "McpToGrpcJson: Unable to build proto descriptor pool");
    return;
  }
}
void McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl::addBuiltinSymbolDescriptor(
    const std::string& symbol_name) {
  if (descriptor_pool_.FindFileContainingSymbol(symbol_name) != nullptr) {
    return;
  }
  auto* builtin_pool = Protobuf::DescriptorPool::generated_pool();
  if (!builtin_pool) {
    return;
  }
  Protobuf::DescriptorPoolDatabase pool_database(*builtin_pool);
  Protobuf::FileDescriptorProto file_proto;
  pool_database.FindFileContainingSymbol(symbol_name, &file_proto);
  addFileDescriptor(file_proto);
}
absl::Status McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl::resolveField(
    const Protobuf::Descriptor* descriptor, const std::string& field_path_str,
    std::vector<const ProtobufWkt::Field*>* field_path) {
  const ProtobufWkt::Type* message_type =
      type_helper_->Info()->GetTypeByTypeUrl(Grpc::Common::typeUrl(descriptor->full_name()));
  if (message_type == nullptr) {
    return {absl::StatusCode::kNotFound, "Could not resolve type: " + descriptor->full_name()};
  }
  absl::Status status = type_helper_->ResolveFieldPath(
      *message_type, field_path_str == "*" ? "" : field_path_str, field_path);
  return status;
}
absl::Status McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl::createMethodInfo(
    const Protobuf::MethodDescriptor* descriptor, const google::api::HttpRule& http_rule,
    MethodInfoSharedPtr& method_info) {
  method_info = std::make_shared<MethodInfo>();
  method_info->descriptor_ = descriptor;
  absl::Status status = resolveField(descriptor->input_type(), http_rule.body(),
                                     &method_info->request_body_field_path);
  if (!status.ok()) {
    return status;
  }
  status = resolveField(descriptor->output_type(), http_rule.response_body(),
                        &method_info->response_body_field_path);
  if (!status.ok()) {
    return status;
  }
  return status;
}
absl::Status McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl::createTranscoder(
    const Envoy::Http::RequestHeaderMap& headers, Protobuf::io::ZeroCopyInputStream& request_input,
    google::grpc::transcoding::TranscoderInputStream& response_input,
    std::unique_ptr<Transcoder>& transcoder, MethodInfoSharedPtr& method_info) const {
  const std::string method(headers.getMethodValue());
  std::string path(headers.getPathValue());
  std::string args;
  const size_t pos = path.find('?');
  if (pos != std::string::npos) {
    args = path.substr(pos + 1);
    path = path.substr(0, pos);
  }
  google::grpc::transcoding::RequestInfo request_info;
  request_info.case_insensitive_enum_parsing = case_insensitive_enum_parsing_;
  std::vector<VariableBinding> variable_bindings;
  method_info =
      path_matcher_->Lookup(method, path, args, &variable_bindings, &request_info.body_field_path);
  if (!method_info) {
    return {absl::StatusCode::kNotFound, "Could not resolve " + path + " to a method."};
  }
  auto status = methodToRequestInfo(method_info, &request_info);
  if (!status.ok()) {
    return status;
  }
  RequestMessageTranslatorPtr request_translator;
  JsonRequestTranslatorPtr json_request_translator;
  json_request_translator = std::make_unique<JsonRequestTranslator>(
      type_helper_->Resolver(), &request_input, std::move(request_info),
      method_info->descriptor_->client_streaming(), true);
  const auto response_type_url =
      Grpc::Common::typeUrl(method_info->descriptor_->output_type()->full_name());
  ResponseToJsonTranslatorPtr response_translator{new ResponseToJsonTranslator(
      type_helper_->Resolver(), response_type_url, method_info->descriptor_->server_streaming(),
      &response_input, response_translate_options_)};
  transcoder = std::make_unique<TranscoderImpl>(std::move(request_translator),
                                                std::move(json_request_translator),
                                                std::move(response_translator));
  return {};
}
absl::Status McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl::methodToRequestInfo(
    const MethodInfoSharedPtr& method_info, google::grpc::transcoding::RequestInfo* info) const {
  const std::string& request_type_full_name = method_info->descriptor_->input_type()->full_name();
  auto request_type_url = Grpc::Common::typeUrl(request_type_full_name);
  info->message_type = type_helper_->Info()->GetTypeByTypeUrl(request_type_url);
  if (info->message_type == nullptr) {
    ENVOY_LOG(debug, "Cannot resolve input-type: {}", request_type_full_name);
    return {absl::StatusCode::kNotFound, "Could not resolve type: " + request_type_full_name};
  }
  return absl::OkStatus();
}
bool McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl::readToBuffer(
    Protobuf::io::ZeroCopyInputStream& stream, Buffer::Instance& data) const {
  const void* out;
  int size;
  while (stream.Next(&out, &size)) {
    if (size == 0) {
      return true;
    }
    data.add(out, size);
  }
  return false;
}
absl::Status McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl::getToolsListJson(
    nlohmann::json& result) const {
  if (tool_registry_.empty()) {
    return absl::InternalError("No tools found");
  }
  result[ResultNextCursor] = "";
  result[ResultTools] = nlohmann::json::array();
  for (const auto& kv : tool_registry_) {
    nlohmann::json tool_json;
    tool_json[ToolName] = kv.second.tool_name;
    tool_json[ToolDescription] = kv.second.description;
    tool_json[ToolInputSchema] = kv.second.input_schema;
    if (!kv.second.output_schema.is_null() && !kv.second.output_schema.empty()) {
      tool_json[ToolOutputSchema] = kv.second.output_schema;
    }
    result[ResultTools].push_back(tool_json);
  }
  ENVOY_LOG(debug, "McpToGrpcJson: getToolsListJson: result: {}", result.dump());
  return absl::OkStatus();
}
absl::Status McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl::handleMcpToolsCall(
    const Envoy::Http::McpHandler::McpRequest& mcp_req, Envoy::Http::RequestHeaderMap& headers,
    Buffer::Instance& body) const {
  if (mcp_req.params.find(ToolName) == mcp_req.params.end()) {
    return absl::InvalidArgumentError("Tool name is not provided");
  }
  std::string tool_name = mcp_req.params[ToolName];
  auto it = tool_registry_.find(tool_name);
  if (it == tool_registry_.end()) {
    return absl::NotFoundError("Unknown tool: " + tool_name);
  }
  const ToolInfo& tool = it->second;
  if (tool.grpc_path.empty()) {
    return absl::InternalError("Tool " + tool_name + " does not have a grpc path");
  }
  std::string http_path = tool.grpc_path;
  ENVOY_LOG(debug, "McpToGrpcJson: handleMcpToolsCall: http_path: {}", http_path);
  headers.setPath(http_path);
  headers.setReferenceMethod(Envoy::Http::Headers::get().MethodValues.Post);
  headers.setReferenceContentType(Envoy::Http::Headers::get().ContentTypeValues.Json);
  headers.setReferenceTE(Envoy::Http::Headers::get().TEValues.Trailers);
  // Build json body
  std::string json_body = mcp_req.params[ToolsCallArguments].dump();
  const auto status = createTranscoder(headers, request_in_, response_in_, transcoder_, method_);
  if (!status.ok()) {
    ENVOY_LOG(debug, "Failed to transcode request headers: {}", status.message());
    if (status.code() == absl::StatusCode::kNotFound) {
      ENVOY_LOG(debug, "Request is not transcoded because it cannot be mapped "
                       "to a gRPC method.");
      return status;
    }
    if (status.code() == absl::StatusCode::kInvalidArgument) {
      ENVOY_LOG(debug, "Request is not transcoded because it contains unknown "
                       "query parameters.");
      return status;
    }
    ENVOY_LOG(debug, "Request is rejected due to strict rejection policy.");
    return absl::InternalError(status.message());
  }
  Buffer::OwnedImpl data_in(std::move(json_body));
  request_in_.move(data_in);
  request_in_.finish();
  body.drain(body.length());
  readToBuffer(*transcoder_->RequestOutput(), body);
  headers.setReferenceContentType(Envoy::Http::Headers::get().ContentTypeValues.Grpc);
  headers.setPath("/" + method_->descriptor_->service()->full_name() + "/" +
                  method_->descriptor_->name());
  if (!transcoder_->RequestStatus().ok()) {
    return absl::InternalError("Request transcoder failed");
  }
  headers.setContentLength(body.length());
  ENVOY_LOG(debug, "McpToGrpcJson: handleMcpToolsCall: transcoded request: {}", body.toString());
  return absl::OkStatus();
}
absl::Status McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl::buildMcpResponse(
    Envoy::Http::ResponseHeaderMap& headers, Buffer::Instance& body,
    Envoy::Http::McpHandler::McpResponse& mcp_resp) const {
  if (!Grpc::Common::isGrpcResponseHeaders(headers, false)) {
    ENVOY_LOG(debug,
              "Response headers is NOT application/grpc content-type. Response is passed through "
              "without transcoding.");
    return absl::InternalError(
        "Response headers is NOT application/grpc content-type. Response is passed through "
        "without transcoding.");
  }
  if (!transcoder_) {
    ENVOY_LOG(debug, "No grpc transcoder");
    return absl::InternalError("No grpc transcoder");
  }
  response_headers_ = &headers;
  headers.setReferenceContentType(Envoy::Http::Headers::get().ContentTypeValues.Json);
  response_in_.move(body);
  response_in_.finish();
  readToBuffer(*transcoder_->ResponseOutput(), response_out_);
  if (!transcoder_->ResponseStatus().ok()) {
    return absl::InternalError("Response transcoder failed");
  }
  // TODO: check if the response is a error message
  // 1. Read the body as a string
  absl::string_view json_str(
      static_cast<const char*>(response_out_.linearize(response_out_.length())),
      response_out_.length());
  if (response_out_.length() == 0) {
    // No body, treat as error or empty result
    return absl::OkStatus();
  }
  ENVOY_LOG(debug, "McpToGrpcJson: transcoder result: {}", json_str);
  // 2. Try to parse the JSON body
  try {
    mcp_resp.result = nlohmann::json::parse(json_str);
  } catch (const std::exception& e) {
    // If parsing fails, set error
    return absl::InternalError("Failed to parse JSON response: " + std::string(e.what()));
  }
  ENVOY_LOG(debug, "McpToGrpcJson: mcp_resp result: {}", mcp_resp.result.dump());
  return absl::OkStatus();
}
McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerFactory(
    const envoy::extensions::http::mcp_handler::mcp_to_grpc::v3::McpToGrpc& proto_config,
    Server::Configuration::CommonFactoryContext& context)
    : proto_config_(proto_config), context_(context) {}
Envoy::Http::McpHandlerPtr McpToGrpcMcpHandlerFactory::create() const {
  return std::make_unique<McpToGrpcMcpHandlerFactory::McpToGrpcMcpHandlerImpl>(proto_config_,
                                                                               context_);
}
} // namespace McpToGrpc
} // namespace McpHandler
} // namespace Http
} // namespace Extensions
} // namespace Envoy
