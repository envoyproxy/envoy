#include "envoy/extensions/filters/http/ai_session/v3/mcp_auth.pb.h"
#include "envoy/extensions/filters/http/ai_session/v3/mcp_auth.pb.validate.h"
#include "envoy/extensions/filters/http/ai_session/v3/mcp_context.pb.h"
#include "envoy/extensions/filters/http/ai_session/v3/mcp_context.pb.validate.h"
#include "envoy/extensions/filters/http/ai_session/v3/mcp_init.pb.h"
#include "envoy/extensions/filters/http/ai_session/v3/mcp_init.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/ai_session/ai_filter_config_factory.h"
#include "source/extensions/filters/http/ai_session/example_filters.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiSession {

// Short-hand aliases for the generated proto types.
using McpAuthProto =
    envoy::extensions::filters::http::ai_session::v3::McpAuthConfig;
using McpInitProto =
    envoy::extensions::filters::http::ai_session::v3::McpInitConfig;
using McpContextProto =
    envoy::extensions::filters::http::ai_session::v3::McpContextConfig;

// ---------------------------------------------------------------------------
// McpAuthFilterFactory
//
// Registers "envoy.ai_filters.mcp_auth".
//
// Reads identity_header and admin_method_prefix from McpAuthConfig.
// The filter allows "initialize" unconditionally; all other methods require
// a session identity.  Admin-prefixed methods require principal == "admin".
// ---------------------------------------------------------------------------
class McpAuthFilterFactory : public NamedAiFilterConfigFactory {
public:
  AiFilterFactory createAiFilterFactory(const Protobuf::Message& proto_config) override {
    // Config is consumed here (at config-load time), not per-request.
    // Fields with empty-string defaults fall back to the filter's built-ins.
    const auto& cfg = MessageUtil::downcastAndValidate<const McpAuthProto&>(
        proto_config, ProtobufMessage::getNullValidationVisitor());

    // Capture config values by value so the lambda is self-contained.
    const std::string identity_header =
        cfg.identity_header().empty() ? "x-mcp-identity" : cfg.identity_header();
    const std::string admin_prefix =
        cfg.admin_method_prefix().empty() ? "admin/" : cfg.admin_method_prefix();

    return [identity_header, admin_prefix]() -> std::unique_ptr<AiStreamFilter> {
      // McpAuthFilter currently uses hardcoded defaults; these will be
      // threaded through once the filter accepts a config struct.
      // TODO: pass identity_header and admin_prefix to McpAuthFilter ctor.
      return std::make_unique<McpAuthFilter>();
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<McpAuthProto>();
  }

  std::string name() const override { return "envoy.ai_filters.mcp_auth"; }
};

// ---------------------------------------------------------------------------
// McpInitFilterFactory
//
// Registers "envoy.ai_filters.mcp_init".
//
// Reads protocol_version and allowed_before_init from McpInitConfig.
// The filter rejects any non-initialize request on an uninitialised session.
// On "initialize" it stores client capabilities in the AiSession.
// ---------------------------------------------------------------------------
class McpInitFilterFactory : public NamedAiFilterConfigFactory {
public:
  AiFilterFactory createAiFilterFactory(const Protobuf::Message& proto_config) override {
    const auto& cfg = MessageUtil::downcastAndValidate<const McpInitProto&>(
        proto_config, ProtobufMessage::getNullValidationVisitor());

    const std::string protocol_version =
        cfg.protocol_version().empty() ? "2024-11-05" : cfg.protocol_version();

    return [protocol_version]() -> std::unique_ptr<AiStreamFilter> {
      // TODO: pass protocol_version to McpInitFilter ctor.
      return std::make_unique<McpInitFilter>();
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<McpInitProto>();
  }

  std::string name() const override { return "envoy.ai_filters.mcp_init"; }
};

// ---------------------------------------------------------------------------
// McpContextFilterFactory
//
// Registers "envoy.ai_filters.mcp_context".
//
// Reads max_context_turns and tracked_methods from McpContextConfig.
// The filter appends each completed exchange to the session context window.
// ---------------------------------------------------------------------------
class McpContextFilterFactory : public NamedAiFilterConfigFactory {
public:
  AiFilterFactory createAiFilterFactory(const Protobuf::Message& proto_config) override {
    MessageUtil::downcastAndValidate<const McpContextProto&>(
        proto_config, ProtobufMessage::getNullValidationVisitor());

    return []() -> std::unique_ptr<AiStreamFilter> {
      // TODO: pass max_turns and tracked_methods to McpContextFilter ctor.
      return std::make_unique<McpContextFilter>();
    };
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<McpContextProto>();
  }

  std::string name() const override { return "envoy.ai_filters.mcp_context"; }
};

// ---------------------------------------------------------------------------
// Static registrations.
//
// These three lines make the factories discoverable via:
//   Config::Utility::getAndCheckFactory<NamedAiFilterConfigFactory>(entry)
// where entry is a TypedExtensionConfig from the AiSession.ai_filters list.
// ---------------------------------------------------------------------------
REGISTER_FACTORY(McpAuthFilterFactory, NamedAiFilterConfigFactory);
REGISTER_FACTORY(McpInitFilterFactory, NamedAiFilterConfigFactory);
REGISTER_FACTORY(McpContextFilterFactory, NamedAiFilterConfigFactory);

} // namespace AiSession
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
