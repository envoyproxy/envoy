#include "extensions/filters/common/auth/attribute_context.h"

#include "common/http/utility.h"
#include "common/network/utility.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Auth {

void AttributeContextUtils::setSourcePeer(envoy::service::auth::v2alpha::AttributeContext& context,
                                          const Network::Connection& connection,
                                          const std::string& service) {
  setPeer(*context.mutable_source(), connection, service, false);
}

void AttributeContextUtils::setDestinationPeer(
    envoy::service::auth::v2alpha::AttributeContext& context,
    const Network::Connection& connection) {
  setPeer(*context.mutable_destination(), connection, "", true);
}

void AttributeContextUtils::setPeer(envoy::service::auth::v2alpha::AttributeContext_Peer& peer,
                                    const Network::Connection& connection,
                                    const std::string& service, const bool local) {

  auto addr = local ? connection.localAddress() : connection.remoteAddress();
  Envoy::Network::Utility::addressToProtobufAddress(*addr, *peer.mutable_address());

  if (!service.empty()) {
    peer.set_service(service);
  }

  auto ssl = const_cast<Ssl::Connection*>(connection.ssl());
  if (ssl) {
    std::string principal = local ? ssl->uriSanLocalCertificate() : ssl->uriSanPeerCertificate();
    if (principal.empty()) {
      principal = local ? ssl->subjectLocalCertificate() : ssl->subjectPeerCertificate();
    }
    peer.set_principal(principal);
  }
}

void AttributeContextUtils::setHttpRequest(
    envoy::service::auth::v2alpha::AttributeContext& context,
    const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
    const Envoy::Http::HeaderMap& headers) {

  auto req = context.mutable_request()->mutable_http();
  auto sdfc = const_cast<Envoy::Http::StreamDecoderFilterCallbacks*>(callbacks);

  auto start = ProtobufUtil::TimeUtil::MicrosecondsToTimestamp(
      sdfc->requestInfo().startTime().time_since_epoch().count());
  context.mutable_request()->mutable_time()->MergeFrom(start);

  auto proto = sdfc->requestInfo().protocol();
  if (proto) {
    req->set_protocol(Envoy::Http::Utility::getProtocolString(proto.value()));
  }

  req->set_id(std::to_string(sdfc->streamId()));
  req->set_size(sdfc->requestInfo().bytesReceived());

  req->set_method(getHeaderStr(headers.Method()));
  req->set_path(getHeaderStr(headers.Path()));
  req->set_host(getHeaderStr(headers.Host()));
  req->set_scheme(getHeaderStr(headers.Scheme()));
  // TODO(rodaine): add query & fragment fields

  auto mutable_headers = req->mutable_headers();
  headers.iterate(
      [](const Envoy::Http::HeaderEntry& e, void* ctx) {
        auto mutable_headers = static_cast<
            Envoy::Protobuf::Map<Envoy::ProtobufTypes::String, Envoy::ProtobufTypes::String>*>(ctx);
        (*mutable_headers)[std::string(e.key().getStringView())] =
            std::string(e.value().getStringView());
        return Envoy::Http::HeaderMap::Iterate::Continue;
      },
      mutable_headers);
}

std::string AttributeContextUtils::getHeaderStr(const Envoy::Http::HeaderEntry* entry) {
  return entry ? std::string(entry->value().getStringView()) : "";
}

} // namespace Auth
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
