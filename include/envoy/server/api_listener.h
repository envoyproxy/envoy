#pragma once

namespace Envoy {
namespace Server {

/**
 * Handle provided to users to interact with Envoy via a particular API.
 * Given the flexibility of the api_listener config, this interface is also maximally flexible.
 * Consumers of this interface have to cast this handle to narrower types.
 */
class ApiListenerHandle {
public:
  virtual ~ApiListenerHandle() = default;
};

/**
 * Listener that provides an API to interact with Envoy via a handle.
 */
class ApiListener {
public:
  virtual ~ApiListener() = default;

  /**
   * An ApiListener is uniquely identified by its name.
   *
   * @return the name of the ApiListener.
   */
  virtual absl::string_view name() const PURE;

  /**
   * @return a handle for interaction, nullptr if the Listener can not construct one.
   */
  virtual ApiListenerHandle* handle() PURE;
};

using ApiListenerPtr = std::unique_ptr<ApiListener>;

} // namespace Server
} // namespace Envoy