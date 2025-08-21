#pragma once
#include <memory>
#include <string>
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/buffer/buffer.h"
#include "envoy/server/factory_context.h"
#include "nlohmann/json.hpp"
#include "absl/status/status.h"
namespace Envoy {
namespace Http {
/**
 * Interface class for session state. Session state is used to get address of upstream host
 * assigned to the session.
 */
class McpHandler {
public:
  struct McpRequest {
    std::string jsonrpc;        // "2.0"
    int64_t id;             // request id
    std::string method;         // tools/list, tools/call
    nlohmann::json params;      // params object, structure depends on method
    std::string mcp_session_id; // MCP Session Id
  };
  struct McpResponse {
    std::string jsonrpc;        // "2.0"
    int64_t id;             // request id
    nlohmann::json result;      // result object
    std::string error;       // error message
    std::string mcp_session_id; // MCP Session Id
  };
  virtual ~McpHandler() = default;
  /**
   * Returns a JSON object containing the list of tools
   * @param result: JSON object containing the list of tools
   * including:
   * - tool name
   * - tool description
   * - tool input schema
   * - tool output schema(optional)
   * @return status of the conversion
   * possible status:
   * OkStatus-success
   * InternalError-internal error, cannot get tools list
   */
  virtual absl::Status getToolsListJson(nlohmann::json &result) const PURE;
  /**
   * Handle an MCP message (json-rpc tools/call)
   * May directly modify headers and body
   * @param mcp_req: MCP request message
   * @param headers: used for output
   * @param body: used for output
   * @return status of the conversion
   * possible status:
   * OkStatus-success
   * InternalError-internal error
   * InvalidArgument-invalid argument
   * NotFoundError-tool not found
   */
  virtual absl::Status handleMcpToolsCall(
      const McpRequest& mcp_req,
      Http::RequestHeaderMap& headers, Buffer::Instance& body) const PURE;
  /**
   * Build an MCP JSON-RPC response
   * May directly modify mcp_resp
   * @param headers: used for input
   * @param body: used for input  
   * @param mcp_resp: MCP response
   * @return status of the build
   * possible status:
   * OkStatus-success
   * InternalError-internal error
   */
  virtual absl::Status buildMcpResponse(
      Http::ResponseHeaderMap& headers, Buffer::Instance& body,
      McpResponse& mcp_resp) const PURE;
};
using McpHandlerPtr = std::unique_ptr<McpHandler>;
/**
 * Interface class for creating MCP handler from configuration.
 */
class McpHandlerFactory {
public:
  virtual ~McpHandlerFactory() = default;
  virtual McpHandlerPtr create() const PURE;
};
using McpHandlerFactorySharedPtr = std::shared_ptr<McpHandlerFactory>;
/*
 * Extension configuration for MCP handler factory.
 */
class McpHandlerFactoryConfig : public Envoy::Config::TypedFactory {
public:
  ~McpHandlerFactoryConfig() override = default;
  /**
   * Creates a particular MCP handler factory implementation.
   *
   * @param config supplies the configuration for the MCP handler factory extension.
   * @return McpHandlerFactorySharedPtr the MCP handler factory.
   */
  virtual McpHandlerFactorySharedPtr
  createMcpHandlerFactory(const Protobuf::Message& config,
                          Server::Configuration::CommonFactoryContext& context) PURE;
  std::string category() const override { return "envoy.http.mcp_handler"; }
};
using McpHandlerFactoryConfigPtr = std::unique_ptr<McpHandlerFactoryConfig>;
} // namespace Http
} // namespace Envoy
