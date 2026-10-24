#pragma once

#include "envoy/http/codec.h"
#include "envoy/http/header_map.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/websocket/handshake.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {

// Adds the Sec-WebSocket-Accept an RFC 6455 server would send for the request's key, if any.
inline Http::TestResponseHeaderMapImpl
withWebSocketAccept(const Http::RequestHeaderMap& request_headers,
                    Http::TestResponseHeaderMapImpl response_headers) {
  const auto keys = request_headers.get(Http::Headers::get().SecWebSocketKey);
  if (keys.size() == 1) {
    response_headers.setCopy(Http::Headers::get().SecWebSocketAccept,
                             WebSocket::computeAccept(keys[0]->value().getStringView()));
  }
  return response_headers;
}

// Removes the sec-websocket-key an HTTP/1.1 codec generated because the original request had none.
// Integration test clients are Envoy codecs, so HTTP/1.1 downstreams generate keys too.
inline void removeGeneratedWebSocketKey(Http::RequestHeaderMap& proxied_request_headers,
                                        const Http::RequestHeaderMap& original_request_headers,
                                        Http::CodecType downstream_protocol,
                                        Http::CodecType upstream_protocol) {
  if (!Http::Utility::isWebSocketUpgradeRequest(original_request_headers) ||
      !original_request_headers.get(Http::Headers::get().SecWebSocketKey).empty()) {
    return;
  }
  const bool expect_generated_key =
      downstream_protocol == Http::CodecType::HTTP1 || upstream_protocol == Http::CodecType::HTTP1;
  EXPECT_EQ(proxied_request_headers.get(Http::Headers::get().SecWebSocketKey).size(),
            expect_generated_key ? 1 : 0);
  proxied_request_headers.remove(Http::Headers::get().SecWebSocketKey);
}

} // namespace Envoy
