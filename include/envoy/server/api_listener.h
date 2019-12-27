#pragma once

namespace Envoy {
namespace Server {

/**
 * Handle provided to users to interact with Envoy via a particular API.
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

  virtual absl::string_view name() const PURE;

  virtual ApiListenerHandle* handle() PURE;
};

using ApiListenerPtr = std::unique_ptr<ApiListener>;

} // namespace Server
} // namespace Envoy