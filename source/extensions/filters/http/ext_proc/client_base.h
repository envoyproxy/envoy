#pragma once

#include <memory>

#include "envoy/service/ext_proc/v3/external_processor.pb.h"

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
  virtual void onComplete(envoy::service::ext_proc::v3::ProcessingResponse& response) PURE;
  virtual void onError() PURE;
};

/**
 * Stream base class used during external processing.
 */
class StreamBase {
public:
  virtual ~StreamBase() = default;
};

/**
 * Async client base class used during external processing.
 */
class ClientBase {
public:
  virtual ~ClientBase() = default;
  virtual void sendRequest(envoy::service::ext_proc::v3::ProcessingRequest&& request,
                           bool end_stream, const uint64_t stream_id, RequestCallbacks* callbacks,
                           StreamBase* stream) PURE;
  virtual void cancel() PURE;
};

using ClientBasePtr = std::unique_ptr<ClientBase>;

} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
