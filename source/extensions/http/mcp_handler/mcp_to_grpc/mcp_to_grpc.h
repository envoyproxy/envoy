#pragma once
#include "source/extensions/http/mcp_handler/mcp_to_grpc/transcoder_input_stream_impl.h"
#include <nlohmann/json.hpp>
#include "envoy/api/api.h"
#include "envoy/buffer/buffer.h"
#include "envoy/extensions/http/mcp_handler/mcp_to_grpc/v3/mcp_to_grpc.pb.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "envoy/http/mcp_handler.h"
#include "google/api/http.pb.h"
#include "grpc_transcoding/path_matcher.h"
#include "grpc_transcoding/request_message_translator.h"
#include "grpc_transcoding/response_to_json_translator.h"
#include "grpc_transcoding/transcoder.h"
#include "grpc_transcoding/type_helper.h"
namespace Envoy {
namespace Extensions {
namespace Http {
namespace McpHandler {
namespace McpToGrpc {
/**
 * VariableBinding specifies a value for a single field in the request message.
 * When transcoding HTTP/REST/JSON to gRPC/proto the request message is
 * constructed using the HTTP body and the variable bindings (specified through
 * request url).
 * See https://github.com/googleapis/googleapis/blob/master/google/api/http.proto
 * for details of variable binding.
 */
 struct VariableBinding {
  // The location of the field in the protobuf message, where the value
  // needs to be inserted, e.g. "shelf.theme" would mean the "theme" field
  // of the nested "shelf" message of the request protobuf message.
  std::vector<std::string> field_path;
  // The value to be inserted.
  std::string value;
};
struct MethodInfo {
  const Protobuf::MethodDescriptor* descriptor_ = nullptr;
  std::vector<const ProtobufWkt::Field*> request_body_field_path;
  std::vector<const ProtobufWkt::Field*> response_body_field_path;
  bool request_type_is_http_body_ = false;
  bool response_type_is_http_body_ = false;
};
using MethodInfoSharedPtr = std::shared_ptr<MethodInfo>;
class McpToGrpcMcpHandlerFactory : public Envoy::Http::McpHandlerFactory {
public:
  ~McpToGrpcMcpHandlerFactory() override = default;
  class McpToGrpcMcpHandlerImpl : public Envoy::Http::McpHandler,
                                  public Logger::Loggable<Logger::Id::config> {
  public:
    struct ToolInfo {
      std::string tool_name;        // Tool name, e.g. "Service.Method"
      std::string description;      // Mcp tool description
      nlohmann::json input_schema;  // JSON Schema for input parameters
      nlohmann::json output_schema; // JSON Schema for output parameters
      std::string grpc_path;        // gRPC path, e.g. "/package.Service/Method"
    };
    McpToGrpcMcpHandlerImpl(
        const envoy::extensions::http::mcp_handler::mcp_to_grpc::v3::McpToGrpc& proto_config,
        Server::Configuration::CommonFactoryContext& context);
    absl::Status getToolsListJson(nlohmann::json& result) const override;
    absl::Status handleMcpToolsCall(const Envoy::Http::McpHandler::McpRequest& mcp_req,
                                    Envoy::Http::RequestHeaderMap& headers,
                                    Buffer::Instance& body) const override;
    absl::Status buildMcpResponse(Envoy::Http::ResponseHeaderMap& headers, Buffer::Instance& body,
                                  Envoy::Http::McpHandler::McpResponse& mcp_resp) const override;
  private:
    absl::Status methodToRequestInfo(const MethodInfoSharedPtr& method_info,
                                   google::grpc::transcoding::RequestInfo* info) const;
    void addFileDescriptor(const Protobuf::FileDescriptorProto& file);
    void addBuiltinSymbolDescriptor(const std::string& symbol_name);
    absl::Status resolveField(const Protobuf::Descriptor* descriptor,
                            const std::string& field_path_str,
                            std::vector<const ProtobufWkt::Field*>* field_path);
    absl::Status createMethodInfo(const Protobuf::MethodDescriptor* method_descriptor,
                          const google::api::HttpRule& http_rule,
                          MethodInfoSharedPtr& method_info);
    absl::Status createTranscoder(const Envoy::Http::RequestHeaderMap& headers,
                          Protobuf::io::ZeroCopyInputStream& request_input,
                          google::grpc::transcoding::TranscoderInputStream& response_input,
                          std::unique_ptr<google::grpc::transcoding::Transcoder>& transcoder,
                          MethodInfoSharedPtr& method_info) const;
    bool readToBuffer(Protobuf::io::ZeroCopyInputStream& stream, Buffer::Instance& data) const;
    Protobuf::DescriptorPool descriptor_pool_;
    std::unique_ptr<google::grpc::transcoding::TypeHelper> type_helper_;
    google::grpc::transcoding::PathMatcherPtr<MethodInfoSharedPtr> path_matcher_;
    std::map<std::string, ToolInfo> tool_registry_;
    google::grpc::transcoding::JsonResponseTranslateOptions response_translate_options_;
    bool case_insensitive_enum_parsing_;
    mutable std::unique_ptr<google::grpc::transcoding::Transcoder> transcoder_;
    mutable TranscoderInputStreamImpl request_in_;
    mutable TranscoderInputStreamImpl response_in_;
    mutable Buffer::OwnedImpl response_out_;
    mutable MethodInfoSharedPtr method_;
    mutable Envoy::Http::ResponseHeaderMap* response_headers_{};
    bool include_output_schema_;
  };
  McpToGrpcMcpHandlerFactory(
      const envoy::extensions::http::mcp_handler::mcp_to_grpc::v3::McpToGrpc& proto_config,
      Server::Configuration::CommonFactoryContext& context);
  Envoy::Http::McpHandlerPtr create() const override;
private:
  const envoy::extensions::http::mcp_handler::mcp_to_grpc::v3::McpToGrpc proto_config_;
  Server::Configuration::CommonFactoryContext& context_;
};
} // namespace McpToGrpc
} // namespace McpHandler
} // namespace Http
} // namespace Extensions
} // namespace Envoy
