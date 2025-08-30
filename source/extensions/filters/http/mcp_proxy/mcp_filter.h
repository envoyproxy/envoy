#pragma once
#include "envoy/http/filter.h"
#include "envoy/buffer/buffer.h"
#include <string>
#include <memory>
#include <nlohmann/json.hpp>
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/logger.h"
#include "envoy/extensions/filters/http/mcp_proxy/v3/mcp_proxy.pb.h"
#include "envoy/http/mcp_handler.h"
namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace McpProxy {
using ProtoConfig = envoy::extensions::filters::http::mcp_proxy::v3::McpProxy;
class McpFilterConfig : public Router::RouteSpecificFilterConfig {
public:
  McpFilterConfig(const ProtoConfig& config, Server::Configuration::CommonFactoryContext& context);
  std::string getServerName() const { return server_name_; }
  std::string getServerTitle() const { return server_title_; }
  std::string getServerVersion() const { return server_version_; }
  std::string getServerInstructions() const { return server_instructions_; }
  bool getStructuredContent() const { return structured_content_; }
  std::unique_ptr<Envoy::Http::McpHandler> createMcpHandler() const {
    ASSERT(factory_ != nullptr);
    return factory_->create();
  }
private:
  Http::McpHandlerFactorySharedPtr factory_;
  std::string server_name_;
  std::string server_title_;
  std::string server_version_;
  std::string server_instructions_;
  bool structured_content_{false};
};
using McpFilterConfigSharedPtr = std::shared_ptr<McpFilterConfig>;
class McpFilter : public Http::StreamFilter, public Logger::Loggable<Logger::Id::filter> {
public:
  McpFilter(const McpFilterConfigSharedPtr& config) : config_(config) {}
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus decodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus decodeTrailers(Http::RequestTrailerMap& trailers) override;
  Http::FilterHeadersStatus encodeHeaders(Http::ResponseHeaderMap& headers,
                                          bool end_stream) override;
  Http::FilterDataStatus encodeData(Buffer::Instance& data, bool end_stream) override;
  Http::FilterTrailersStatus encodeTrailers(Http::ResponseTrailerMap& trailers) override;
  Http::Filter1xxHeadersStatus encode1xxHeaders(Http::ResponseHeaderMap& headers) override;
  Http::FilterMetadataStatus encodeMetadata(Http::MetadataMap& metadata_map) override;
  void onDestroy() override;
  void setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) override {
    decoder_callbacks_ = &callbacks;
  }
  void setEncoderFilterCallbacks(Http::StreamEncoderFilterCallbacks& callbacks) override {
    encoder_callbacks_ = &callbacks;
  }
  void initPerRouteConfig();
  void sendProtocolError(nlohmann::json error_msg);
private:
  const McpFilterConfigSharedPtr config_;
  McpFilterConfigSharedPtr per_route_config_;
  std::unique_ptr<Envoy::Http::McpHandler> mcp_handler_;
  Http::RequestHeaderMap* request_headers_;
  Http::ResponseHeaderMap* response_headers_;
  Http::StreamDecoderFilterCallbacks* decoder_callbacks_{};
  Http::StreamEncoderFilterCallbacks* encoder_callbacks_{};
  absl::optional<int64_t> request_id_;
  Buffer::OwnedImpl request_data_;
  Buffer::OwnedImpl response_data_;
  bool local_reply_sent_{false};
};
} // namespace McpProxy
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
