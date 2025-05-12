#pragma once

#include <memory>
#include <string>

#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/http/header_map.h"
#include "envoy/server/factory_context.h"
#include "envoy/upstream/upstream.h"

namespace Envoy {
namespace Http {

/**
 * Interface class for session state. Session state is used to get address of upstream host
 * assigned to the session.
 */
class SessionState {
public:
  virtual ~SessionState() = default;

  /**
   * Get address of upstream host that the current session stuck on.
   *
   * @return absl::optional<absl::string_view> optional upstream address. If there is no available
   * session or no available address, absl::nullopt will be returned.
   */
  virtual absl::optional<absl::string_view> upstreamAddress() const PURE;

  /**
   * Called when a request is completed to update the session state.
   *
   * @param host the upstream host that was finally selected.
   * @param headers the response headers.
   */
  virtual void onUpdate(const Upstream::HostDescription& host, ResponseHeaderMap& headers) PURE;
};

using SessionStatePtr = std::unique_ptr<SessionState>;

/**
 * Interface class for creating session state from request headers.
 */
class SessionStateFactory {
public:
  virtual ~SessionStateFactory() = default;

  /**
   * Create session state from request headers.
   *
   * @param headers request headers.
   */
  virtual SessionStatePtr create(const RequestHeaderMap& headers) const PURE;
};

using SessionStateFactorySharedPtr = std::shared_ptr<SessionStateFactory>;

/*
 * Extension configuration for session state factory.
 */
class SessionStateFactoryConfig : public Envoy::Config::TypedFactory {
public:
  ~SessionStateFactoryConfig() override = default;

  /**
   * Creates a particular session state factory implementation.
   *
   * @param config supplies the configuration for the session state factory extension.
   * @param context supplies the factory context. Please don't store the reference to
   * the context as it is only valid during the call.
   * @return SessionStateFactorySharedPtr the session state factory.
   */
  virtual SessionStateFactorySharedPtr
  createSessionStateFactory(const Protobuf::Message& config,
                            Server::Configuration::GenericFactoryContext& context) PURE;

  std::string category() const override { return "envoy.http.stateful_session"; }
};

using SessionStateFactoryConfigPtr = std::unique_ptr<SessionStateFactoryConfig>;

} // namespace Http
} // namespace Envoy
