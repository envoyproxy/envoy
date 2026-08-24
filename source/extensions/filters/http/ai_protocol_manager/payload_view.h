#pragma once

#include "source/extensions/filters/http/ai_protocol_manager/api_protocol_adapter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {

class JsonWithExtBuf;

// Read-only views over a stream's AI payload: the seam payload features
// program against so they stay protocol-agnostic. Dialect knowledge lives in
// the adapter a view carries; a feature that needs something a view lacks
// grows the view an accessor rather than reading dialect JSON. Views only
// reference per-stream state and are only valid where they are handed out.
//
// [#comment: these views become the argument surface of the AI payload
// filter chain (the in-filter L8 chain); today the filter's own features --
// schema validation, token-usage publication -- consume them.]

// The fully received, parsed request payload of a declared AI endpoint.
class RequestPayloadView {
public:
  RequestPayloadView(ApiProtocol protocol, const JsonWithExtBuf& document)
      : protocol_(protocol), document_(document) {}

  ApiProtocol protocol() const { return protocol_; }
  const ApiProtocolAdapter& adapter() const { return AdapterRegistry::get(protocol_); }
  // The parsed document, with oversized strings left in the external buffer.
  const JsonWithExtBuf& document() const { return document_; }

private:
  const ApiProtocol protocol_;
  const JsonWithExtBuf& document_;
};

// The finalized token-usage accumulation of a completed response.
class ResponsePayloadView {
public:
  ResponsePayloadView(const TokenUsage& usage, bool degraded)
      : usage_(usage), degraded_(degraded) {}

  ApiProtocol protocol() const { return usage_.api_protocol; }
  const ApiProtocolAdapter& adapter() const { return AdapterRegistry::get(usage_.api_protocol); }
  const TokenUsage& usage() const { return usage_; }
  // Extraction lost input on this stream; the counts may be stale or
  // incomplete.
  bool degraded() const { return degraded_; }

private:
  const TokenUsage& usage_;
  const bool degraded_;
};

} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
