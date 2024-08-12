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
 * Async client used during external processing.
 */
class Client {
public:
  // Destructor

  virtual ~Client() = default;
  virtual void cancel() PURE;
};

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy

