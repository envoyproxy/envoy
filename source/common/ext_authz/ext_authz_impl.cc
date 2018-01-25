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

#include "fmt/format.h"

namespace Envoy {
namespace ExtAuthz {

using ::envoy::api::v2::auth::AttributeContext;
using ::envoy::api::v2::auth::AttributeContext_HttpRequest;
using ::envoy::api::v2::auth::AttributeContext_Peer;
using ::envoy::api::v2::auth::AttributeContext_Request;

GrpcClientImpl::GrpcClientImpl(Grpc::AsyncClientPtr&& async_client,
                               const Optional<std::chrono::milliseconds>& timeout)
    : service_method_(*Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
          "envoy.api.v2.auth.Authorization.Check")),
      async_client_(std::move(async_client)), timeout_(timeout) {}

GrpcClientImpl::~GrpcClientImpl() { ASSERT(!callbacks_); }

void GrpcClientImpl::cancel() {
  ASSERT(callbacks_ != nullptr);
  request_->cancel();
  callbacks_ = nullptr;
}

void GrpcClientImpl::check(RequestCallbacks& callbacks,
                           const envoy::api::v2::auth::CheckRequest& request,
                           Tracing::Span& parent_span) {
  ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;

  request_ = async_client_->send(service_method_, request, *this, parent_span, timeout_);
}

void GrpcClientImpl::onSuccess(std::unique_ptr<envoy::api::v2::auth::CheckResponse>&& response,
                               Tracing::Span& span) {
  CheckStatus status = CheckStatus::OK;
  ASSERT(response->status().code() != Grpc::Status::GrpcStatus::Unknown);
  if (response->status().code() != Grpc::Status::GrpcStatus::Ok) {
    status = CheckStatus::Denied;
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceUnauthz);
  } else {
    span.setTag(Constants::get().TraceStatus, Constants::get().TraceOk);
  }

  callbacks_->complete(status);
  callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string&,
                               Tracing::Span&) {
  ASSERT(status != Grpc::Status::GrpcStatus::Ok);
  UNREFERENCED_PARAMETER(status);
  callbacks_->complete(CheckStatus::Error);
  callbacks_ = nullptr;
}

GrpcFactoryImpl::GrpcFactoryImpl(const envoy::api::v2::GrpcService& grpc_service,
                                 Grpc::AsyncClientManager& async_client_manager,
                                 Stats::Scope& scope) {
  async_client_factory_ = async_client_manager.factoryForGrpcService(grpc_service, scope);
}

ClientPtr GrpcFactoryImpl::create(const Optional<std::chrono::milliseconds>& timeout) {
  return std::make_unique<GrpcClientImpl>(async_client_factory_->create(), timeout);
}

std::unique_ptr<::envoy::api::v2::Address> ExtAuthzCheckRequestGenerator::getProtobufAddress(
    const Network::Address::InstanceConstSharedPtr& instance) {

  std::unique_ptr<::envoy::api::v2::Address> addr =
      std::unique_ptr<::envoy::api::v2::Address>{new ::envoy::api::v2::Address()};
  ASSERT(addr);

  if (instance->type() == Network::Address::Type::Ip) {
    addr->mutable_socket_address()->set_address(instance->ip()->addressAsString());
    addr->mutable_socket_address()->set_port_value(instance->ip()->port());
  } else {
    ASSERT(instance->type() == Network::Address::Type::Pipe);
    addr->mutable_pipe()->set_path(instance->asString());
  }
  return addr;
}

std::unique_ptr<AttributeContext_Peer>
ExtAuthzCheckRequestGenerator::getConnectionPeer(const Network::Connection* connection,
                                                 const std::string& service, const bool local) {

  std::unique_ptr<AttributeContext_Peer> peer =
      std::unique_ptr<AttributeContext_Peer>{new AttributeContext_Peer()};
  ASSERT(peer);

  // Set the address
  if (!local) {
    peer->set_allocated_address(getProtobufAddress(connection->remoteAddress()).release());
  } else {
    peer->set_allocated_address(getProtobufAddress(connection->localAddress()).release());
  }

  // Set the principal
  // Preferably the SAN from the peer's cert or
  // Subject from the peer's cert.
  std::string principal;
  Ssl::Connection* ssl = const_cast<Ssl::Connection*>(connection->ssl());
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
  peer->set_principal(principal);

  if (!service.empty()) {
    peer->set_service(service);
  }

  return peer;
}

std::unique_ptr<AttributeContext_Peer>
ExtAuthzCheckRequestGenerator::getConnectionPeer(const Network::Connection& connection,
                                                 const std::string& service, const bool local) {
  return getConnectionPeer(&connection, service, local);
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
  ::google::protobuf::Map<::std::string, ::std::string>* mhdrs =
      static_cast<::google::protobuf::Map<::std::string, ::std::string>*>(ctx);
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

std::unique_ptr<AttributeContext_Request> ExtAuthzCheckRequestGenerator::getHttpRequest(
    const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
    const Envoy::Http::HeaderMap& headers) {

  AttributeContext_HttpRequest* httpreq = new AttributeContext_HttpRequest();
  ASSERT(httpreq);

  // Set id
  // The streamId is not qualified as a const. Although it is as it does not modify the object.
  Envoy::Http::StreamDecoderFilterCallbacks* sdfc =
      const_cast<Envoy::Http::StreamDecoderFilterCallbacks*>(callbacks);
  httpreq->set_id(std::to_string(sdfc->streamId()));

  // Set method
  httpreq->set_method(getHeaderStr(headers.Method()));
  // Set path
  httpreq->set_path(getHeaderStr(headers.Path()));
  // Set host
  httpreq->set_host(getHeaderStr(headers.Host()));
  // Set scheme
  httpreq->set_scheme(getHeaderStr(headers.Scheme()));

  // Set size
  // need to convert to google buffer 64t;
  httpreq->set_size(sdfc->requestInfo().bytesReceived());

  // Set protocol
  httpreq->set_protocol(getProtocolStr(sdfc->requestInfo().protocol().value()));

  // Fill in the headers
  auto* mhdrs = httpreq->mutable_headers();
  headers.iterate(fillHttpHeaders, mhdrs);

  std::unique_ptr<AttributeContext_Request> req =
      std::unique_ptr<AttributeContext_Request>{new AttributeContext_Request()};
  ASSERT(req);
  req->set_allocated_http(httpreq);

  return req;
}

void ExtAuthzCheckRequestGenerator::createHttpCheck(
    const Envoy::Http::StreamDecoderFilterCallbacks* callbacks,
    const Envoy::Http::HeaderMap& headers, envoy::api::v2::auth::CheckRequest& request) {

  AttributeContext* attrs = request.mutable_attributes();
  ASSERT(attrs);

  Envoy::Http::StreamDecoderFilterCallbacks* cb =
      const_cast<Envoy::Http::StreamDecoderFilterCallbacks*>(callbacks);

  std::string service = getHeaderStr(headers.EnvoyDownstreamServiceCluster());
  attrs->set_allocated_source(getConnectionPeer(cb->connection(), service, false).release());
  attrs->set_allocated_destination(getConnectionPeer(cb->connection(), "", true).release());
  attrs->set_allocated_request(getHttpRequest(callbacks, headers).release());
}

void ExtAuthzCheckRequestGenerator::createTcpCheck(const Network::ReadFilterCallbacks* callbacks,
                                                   envoy::api::v2::auth::CheckRequest& request) {

  AttributeContext* attrs = request.mutable_attributes();
  ASSERT(attrs);

  Network::ReadFilterCallbacks* cb = const_cast<Network::ReadFilterCallbacks*>(callbacks);
  attrs->set_allocated_source(getConnectionPeer(cb->connection(), "", false).release());
  attrs->set_allocated_destination(getConnectionPeer(cb->connection(), "", true).release());
}

} // namespace ExtAuthz
} // namespace Envoy
