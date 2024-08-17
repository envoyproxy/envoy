#pragma once

#include <memory>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {

/**
 * Async callbacks used during external processing.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;
  virtual void onComplete() PURE;
};

/**
 * Async client base class used during external processing.
 */
class ClientBase {
public:
  virtual ~ClientBase() = default;

  virtual void sendRequest() PURE;
  virtual void cancel() PURE;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
