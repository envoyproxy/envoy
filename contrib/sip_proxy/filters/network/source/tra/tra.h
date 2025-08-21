#pragma once

#include "envoy/common/pure.h"
#include "envoy/singleton/manager.h"
#include "envoy/stream_info/stream_info.h"
#include "envoy/tracing/tracer.h"

#include "absl/container/flat_hash_map.h"
#include "absl/types/any.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace TrafficRoutingAssistant {

using TraContextMap = absl::flat_hash_map<std::string, std::string>;

enum class ResponseType {
  CreateResp,
  UpdateResp,
  RetrieveResp,
  DeleteResp,
  SubscribeResp,
  FailureResp,
};

/**
 * Async callbacks used during grpc calls.
 */
class RequestCallbacks {
public:
  virtual ~RequestCallbacks() = default;

  virtual void complete(const ResponseType& type, const std::string& message_type,
                        const absl::any& resp) PURE;
};

/**
 * A client used to query a centralized TRA service.
 */
class Client {
public:
  virtual ~Client() = default;

  virtual void setRequestCallbacks(RequestCallbacks& callbacks) PURE;
  virtual void createTrafficRoutingAssistant(
      const std::string& type, const absl::flat_hash_map<std::string, std::string>& data,
      const absl::optional<TraContextMap> context, Tracing::Span& parent_span,
      const StreamInfo::StreamInfo& stream_info) PURE;
  virtual void updateTrafficRoutingAssistant(
      const std::string& type, const absl::flat_hash_map<std::string, std::string>& data,
      const absl::optional<TraContextMap> context, Tracing::Span& parent_span,
      const StreamInfo::StreamInfo& stream_info) PURE;
  virtual void retrieveTrafficRoutingAssistant(const std::string& type, const std::string& key,
                                               const absl::optional<TraContextMap> context,
                                               Tracing::Span& parent_span,
                                               const StreamInfo::StreamInfo& stream_info) PURE;
  virtual void deleteTrafficRoutingAssistant(const std::string& type, const std::string& key,
                                             const absl::optional<TraContextMap> context,
                                             Tracing::Span& parent_span,
                                             const StreamInfo::StreamInfo& stream_info) PURE;
  virtual void subscribeTrafficRoutingAssistant(const std::string& type, Tracing::Span& parent_span,
                                                const StreamInfo::StreamInfo& stream_info) PURE;
};

using ClientPtr = std::unique_ptr<Client>;

} // namespace TrafficRoutingAssistant
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
