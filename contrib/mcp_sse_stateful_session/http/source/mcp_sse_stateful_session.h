#pragma once

#include <memory>

#include "envoy/buffer/buffer.h"
#include "envoy/http/filter.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace McpSseSessionState {

/**
 * Independent interface for session state that supports data processing.
 * This is completely independent of the main Envoy stateful session interface.
 */
class McpSseSessionState {
public:
  virtual ~McpSseSessionState() = default;

  /**
   * Get address of upstream host that the current session stuck on.
   *
   * @return absl::optional<absl::string_view> optional upstream address. If there is no available
   * session or no available address, absl::nullopt will be returned.
   */
  virtual absl::optional<absl::string_view> upstreamAddress() const PURE;

  /**
   * Called when response headers are available.
   *
   * @param host_address the upstream host that was selected.
   * @param headers the response headers.
   */
  virtual void onUpdateHeader(absl::string_view host_address,
                              Envoy::Http::ResponseHeaderMap& headers) PURE;

  /**
   * Called when response data is available for processing.
   *
   * @param host_address the upstream host that was selected.
   * @param data the response data buffer.
   * @param end_stream whether this is the end of the stream.
   * @return FilterDataStatus indicating how to proceed with the data.
   */
  virtual Envoy::Http::FilterDataStatus onUpdateData(absl::string_view host_address,
                                                     Buffer::Instance& data, bool end_stream) PURE;
};

using McpSseSessionStatePtr = std::unique_ptr<McpSseSessionState>;

/**
 * Independent interface for creating session state from request headers.
 */
class McpSseSessionStateFactory {
public:
  virtual ~McpSseSessionStateFactory() = default;

  /**
   * Create session state from request headers.
   *
   * @param headers request headers.
   */
  virtual McpSseSessionStatePtr create(Envoy::Http::RequestHeaderMap& headers) const PURE;
};

using McpSseSessionStateFactorySharedPtr = std::shared_ptr<McpSseSessionStateFactory>;

/*
 * Extension configuration for session state factory.
 */
class McpSseSessionStateFactoryConfig : public Envoy::Config::TypedFactory {
public:
  ~McpSseSessionStateFactoryConfig() override = default;

  /**
   * Creates a particular session state factory implementation.
   *
   * @param config supplies the configuration for the session state factory extension.
   * @param context supplies the factory context. Please don't store the reference to
   * the context as it is only valid during the call.
   * @return SessionStateFactorySharedPtr the session state factory.
   */
  virtual McpSseSessionStateFactorySharedPtr
  createSessionStateFactory(const Protobuf::Message& config,
                            Server::Configuration::GenericFactoryContext& context) PURE;

  std::string category() const override { return "envoy.http.mcp_sse_stateful_session"; }
};

using McpSseSessionStateFactoryConfigPtr = std::unique_ptr<McpSseSessionStateFactoryConfig>;

} // namespace McpSseSessionState
} // namespace Http
} // namespace Extensions
} // namespace Envoy
