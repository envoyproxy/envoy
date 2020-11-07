#include "extensions/filters/common/ext_authz/check_request_utils.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/service/auth/v3/attribute_context.pb.h"
#include "envoy/service/auth/v3/external_auth.pb.h"
#include "envoy/ssl/connection.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/enum_to_int.h"
#include "common/grpc/async_client_impl.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/network/utility.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/common/ext_authz/ext_authz.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace ExtAuthz {

void CheckRequestUtils::setAttrContextPeer(envoy::service::auth::v3::AttributeContext::Peer& peer,
                                           const Network::Connection& connection,
                                           const std::string& service, const bool local,
                                           bool include_certificate) {

  // Set the address
  auto addr = peer.mutable_address();
  if (local) {
    Envoy::Network::Utility::addressToProtobufAddress(*connection.localAddress(), *addr);
  } else {
    Envoy::Network::Utility::addressToProtobufAddress(*connection.remoteAddress(), *addr);
  }

  // Set the principal. Preferably the URI SAN, DNS SAN or Subject in that order from the peer's
  // cert. Include the X.509 certificate of the source peer, if configured to do so.
  auto ssl = connection.ssl();
  if (ssl != nullptr) {
    if (local) {
      const auto uri_sans = ssl->uriSanLocalCertificate();
      if (uri_sans.empty()) {
        const auto dns_sans = ssl->dnsSansLocalCertificate();
        if (dns_sans.empty()) {
          peer.set_principal(ssl->subjectLocalCertificate());
        } else {
          peer.set_principal(dns_sans[0]);
        }
      } else {
        peer.set_principal(uri_sans[0]);
      }
    } else {
      const auto uri_sans = ssl->uriSanPeerCertificate();
      if (uri_sans.empty()) {
        const auto dns_sans = ssl->dnsSansPeerCertificate();
        if (dns_sans.empty()) {
          peer.set_principal(ssl->subjectPeerCertificate());
        } else {
          peer.set_principal(dns_sans[0]);
        }
      } else {
        peer.set_principal(uri_sans[0]);
      }
      if (include_certificate) {
        peer.set_certificate(ssl->urlEncodedPemEncodedPeerCertificate());
      }
    }
  }

  if (!service.empty()) {
    peer.set_service(service);
  }
}

std::string CheckRequestUtils::getHeaderStr(const Envoy::Http::HeaderEntry* entry) {
  if (entry) {
    // TODO(jmarantz): plumb absl::string_view further here; there's no need
    // to allocate a temp string in the local uses.
    return std::string(entry->value().getStringView());
  }
  return EMPTY_STRING;
}

void CheckRequestUtils::setRequestTime(envoy::service::auth::v3::AttributeContext::Request& req,
                                       const StreamInfo::StreamInfo& stream_info) {
  // Set the timestamp when the proxy receives the first byte of the request.
  req.mutable_time()->MergeFrom(Protobuf::util::TimeUtil::NanosecondsToTimestamp(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          stream_info.startTime().time_since_epoch())
          .count()));
}

void CheckRequestUtils::setHttpRequest(
    envoy::service::auth::v3::AttributeContext::HttpRequest& httpreq, uint64_t stream_id,
    const StreamInfo::StreamInfo& stream_info, const Buffer::Instance* decoding_buffer,
    const Envoy::Http::RequestHeaderMap& headers, uint64_t max_request_bytes, bool pack_as_bytes) {
  httpreq.set_id(std::to_string(stream_id));
  httpreq.set_method(getHeaderStr(headers.Method()));
  httpreq.set_path(getHeaderStr(headers.Path()));
  httpreq.set_host(getHeaderStr(headers.Host()));
  httpreq.set_scheme(getHeaderStr(headers.Scheme()));
  httpreq.set_size(stream_info.bytesReceived());

  if (stream_info.protocol()) {
    httpreq.set_protocol(Envoy::Http::Utility::getProtocolString(stream_info.protocol().value()));
  }

  // Fill in the headers.
  auto* mutable_headers = httpreq.mutable_headers();
  headers.iterate([mutable_headers](const Envoy::Http::HeaderEntry& e) {
    // Skip any client EnvoyAuthPartialBody header, which could interfere with internal use.
    if (e.key().getStringView() != Headers::get().EnvoyAuthPartialBody.get()) {
      (*mutable_headers)[std::string(e.key().getStringView())] =
          std::string(e.value().getStringView());
    }
    return Envoy::Http::HeaderMap::Iterate::Continue;
  });

  // Set request body.
  if (max_request_bytes > 0 && decoding_buffer != nullptr) {
    const uint64_t length = std::min(decoding_buffer->length(), max_request_bytes);
    std::string data(length, 0);
    decoding_buffer->copyOut(0, length, &data[0]);

    // This pack_as_bytes flag allows us to switch the content type (bytes or string) of "body" to
    // be sent to the external authorization server without doing string encoding check (in this
    // case UTF-8 check).
    if (pack_as_bytes) {
      httpreq.set_raw_body(std::move(data));
    } else {
      httpreq.set_body(std::move(data));
    }

    // Add in a header to detect when a partial body is used.
    (*mutable_headers)[Headers::get().EnvoyAuthPartialBody.get()] =
        length != decoding_buffer->length() ? "true" : "false";
  }
}

void CheckRequestUtils::setAttrContextRequest(
    envoy::service::auth::v3::AttributeContext::Request& req, const uint64_t stream_id,
    const StreamInfo::StreamInfo& stream_info, const Buffer::Instance* decoding_buffer,
    const Envoy::Http::RequestHeaderMap& headers, uint64_t max_request_bytes, bool pack_as_bytes) {
  setRequestTime(req, stream_info);
  setHttpRequest(*req.mutable_http(), stream_id, stream_info, decoding_buffer, headers,
                 max_request_bytes, pack_as_bytes);
}

void CheckRequestUtils::createHttpCheck(
    const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
    const Envoy::Http::RequestHeaderMap& headers,
    Protobuf::Map<std::string, std::string>&& context_extensions,
    envoy::config::core::v3::Metadata&& metadata_context,
    envoy::service::auth::v3::CheckRequest& request, uint64_t max_request_bytes, bool pack_as_bytes,
    bool include_peer_certificate) {

  auto attrs = request.mutable_attributes();
  const std::string service = getHeaderStr(headers.EnvoyDownstreamServiceCluster());

  // *cb->connection(), callbacks->streamInfo() and callbacks->decodingBuffer() are not qualified as
  // const.
  auto* cb = const_cast<Envoy::Http::StreamDecoderFilterCallbacks*>(callbacks);
  setAttrContextPeer(*attrs->mutable_source(), *cb->connection(), service, false,
                     include_peer_certificate);
  setAttrContextPeer(*attrs->mutable_destination(), *cb->connection(), EMPTY_STRING, true,
                     include_peer_certificate);
  setAttrContextRequest(*attrs->mutable_request(), cb->streamId(), cb->streamInfo(),
                        cb->decodingBuffer(), headers, max_request_bytes, pack_as_bytes);

  // Fill in the context extensions and metadata context.
  (*attrs->mutable_context_extensions()) = std::move(context_extensions);
  (*attrs->mutable_metadata_context()) = std::move(metadata_context);
}

void CheckRequestUtils::createTcpCheck(const Network::ReadFilterCallbacks* callbacks,
                                       envoy::service::auth::v3::CheckRequest& request,
                                       bool include_peer_certificate) {

  auto attrs = request.mutable_attributes();

  auto* cb = const_cast<Network::ReadFilterCallbacks*>(callbacks);
  setAttrContextPeer(*attrs->mutable_source(), cb->connection(), "", false,
                     include_peer_certificate);
  setAttrContextPeer(*attrs->mutable_destination(), cb->connection(), "", true,
                     include_peer_certificate);
}

} // namespace ExtAuthz
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
