#include "common/ext_authz/ext_authz_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/access_log/access_log.h"
#include "envoy/ssl/connection.h"

#include "common/common/assert.h"
#include "common/grpc/async_client_impl.h"
#include "common/http/headers.h"
#include "common/protobuf/protobuf.h"

#include "fmt/format.h"

namespace Envoy {
namespace ExtAuthz {

GrpcClientImpl::GrpcClientImpl(Grpc::AsyncClientPtr&& async_client,
                               const Optional<std::chrono::milliseconds>& timeout)
    : service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.service.auth.v2.Authorization.Check")),
      async_client_(std::move(async_client)), timeout_(timeout) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcClientImpl::check(RequestCallbacks& callbacks,
                           const envoy::service::auth::v2::CheckRequest& request,
                           Tracing::Span& parent_span) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  request_ = async_client_->send(service_method_, request, *this, parent_span, timeout_);
}

void GrpcClientImpl::onSuccess(std::unique_ptr<envoy::service::auth::v2::CheckResponse>&& response,
                               Tracing::Span& span) {
  CheckStatus status = CheckStatus::OK;
  ASSERT(response->status().code() != Grpc::Status::GrpcStatus::Unknown);
  if (response->status().code() != Grpc::Status::GrpcStatus::Ok) {
    status = CheckStatus::Denied;
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceUnauthz);
  } else {
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOk);
  }

  callbacks_->onComplete(status);
  callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                               Tracing::Span&) {
  ASSERT(status != Grpc::Status::GrpcStatus::Ok);
  UNREFERENCED_PARAMETER(status);
  callbacks_->onComplete(CheckStatus::Error);
  callbacks_ = nullptr;
}

void ExtAuthzCheckRequestGenerator::addressToProtobufAddress(
    envoy::api::v2::Address& proto_address, const Network::Address::Instance& address) {
  if (address.type() == Network::Address::Type::Pipe) {
    proto_address.mutable_pipe()->set_path(address.asString());
  } else {
    ASSERT(address.type() == Network::Address::Type::Ip);
    auto* socket_address = proto_address.mutable_socket_address();
    socket_address->set_address(address.ip()->addressAsString());
    socket_address->set_port_value(address.ip()->port());
  }
}

void ExtAuthzCheckRequestGenerator::setAttrContextPeer(
    envoy::service::auth::v2::AttributeContext_Peer& peer, const Network::Connection& connection,
    const std::string& service, const bool local) {

  // Set the address
  auto addr = peer.mutable_address();
  if (!local) {
    addressToProtobufAddress(*addr, *connection.remoteAddress());
  } else {
    addressToProtobufAddress(*addr, *connection.localAddress());
  }

  // Set the principal
  // Preferably the SAN from the peer's cert or
  // Subject from the peer's cert.
  std::string principal;
  Ssl::Connection* ssl = const_cast<Ssl::Connection*>(connection.ssl());
  if (ssl != nullptr) {
    if (!local) {
      principal = ssl->uriSanPeerCertificate();

      if (principal.empty()) {
        principal = ssl->subjectPeerCertificate();
      }
    } else {
      principal = ssl->uriSanLocalCertificate();

      if (principal.empty()) {
        principal = ssl->subjectLocalCertificate();
      }
    }
  }
  peer.set_principal(principal);

  if (!service.empty()) {
    peer.set_service(service);
  }
}

const std::string ExtAuthzCheckRequestGenerator::getProtocolStr(const Envoy::Http::Protocol& p) {

  switch (p) {
  case Envoy::Http::Protocol::Http10:
    return std::string("Http1.0");
  case Envoy::Http::Protocol::Http11:
    return std::string("Http1.1");
  case Envoy::Http::Protocol::Http2:
    return std::string("Http2");
  default:
    break;
  }
  return std::string("unknown");
}

Envoy::Http::HeaderMap::Iterate
ExtAuthzCheckRequestGenerator::fillHttpHeaders(const Envoy::Http::HeaderEntry& e, void* ctx) {
  Envoy::Protobuf::Map<::std::string, ::std::string>* mhdrs =
      static_cast<Envoy::Protobuf::Map<::std::string, ::std::string>*>(ctx);
  (*mhdrs)[std::string(e.key().c_str(), e.key().size())] =
      std::string(e.value().c_str(), e.value().size());
  return Envoy::Http::HeaderMap::Iterate::Continue;
}

std::string ExtAuthzCheckRequestGenerator::getHeaderStr(const Envoy::Http::HeaderEntry* entry) {
  if (entry) {
    return std::string(entry->value().c_str(), entry->value().size());
  }
  return "";
}

void ExtAuthzCheckRequestGenerator::setHttpRequest(
    ::envoy::service::auth::v2::AttributeContext_HttpRequest& httpreq,
    const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
    const Envoy::Http::HeaderMap& headers) {

  // Set id
  // The streamId is not qualified as a const. Although it is as it does not modify the object.
  Envoy::Http::StreamDecoderFilterCallbacks* sdfc =
      const_cast<Envoy::Http::StreamDecoderFilterCallbacks*>(callbacks);
  httpreq.set_id(std::to_string(sdfc->streamId()));

  // Set method
  httpreq.set_method(getHeaderStr(headers.Method()));
  // Set path
  httpreq.set_path(getHeaderStr(headers.Path()));
  // Set host
  httpreq.set_host(getHeaderStr(headers.Host()));
  // Set scheme
  httpreq.set_scheme(getHeaderStr(headers.Scheme()));

  // Set size
  // need to convert to google buffer 64t;
  httpreq.set_size(sdfc->requestInfo().bytesReceived());

  // Set protocol
  if (sdfc->requestInfo().protocol().valid()) {
    httpreq.set_protocol(getProtocolStr(sdfc->requestInfo().protocol().value()));
  }

  // Fill in the headers
  auto mhdrs = httpreq.mutable_headers();
  headers.iterate(fillHttpHeaders, mhdrs);
}

void ExtAuthzCheckRequestGenerator::setAttrContextRequest(
    ::envoy::service::auth::v2::AttributeContext_Request& req,
    const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
    const Envoy::Http::HeaderMap& headers) {
  setHttpRequest(*req.mutable_http(), callbacks, headers);
}

void ExtAuthzCheckRequestGenerator::createHttpCheck(
    const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
    const Envoy::Http::HeaderMap& headers, envoy::service::auth::v2::CheckRequest& request) {

  auto attrs = request.mutable_attributes();

  Envoy::Http::StreamDecoderFilterCallbacks* cb =
      const_cast<Envoy::Http::StreamDecoderFilterCallbacks*>(callbacks);

  std::string service = getHeaderStr(headers.EnvoyDownstreamServiceCluster());

  setAttrContextPeer(*attrs->mutable_source(), *cb->connection(), service, false);
  setAttrContextPeer(*attrs->mutable_destination(), *cb->connection(), "", true);
  setAttrContextRequest(*attrs->mutable_request(), callbacks, headers);
}

void ExtAuthzCheckRequestGenerator::createTcpCheck(
    const Network::ReadFilterCallbacks* callbacks,
    envoy::service::auth::v2::CheckRequest& request) {

  auto attrs = request.mutable_attributes();

  Network::ReadFilterCallbacks* cb = const_cast<Network::ReadFilterCallbacks*>(callbacks);
  setAttrContextPeer(*attrs->mutable_source(), cb->connection(), "", false);
  setAttrContextPeer(*attrs->mutable_destination(), cb->connection(), "", true);
}

} // namespace ExtAuthz
} // namespace Envoy
