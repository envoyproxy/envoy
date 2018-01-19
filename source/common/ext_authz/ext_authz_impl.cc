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

GrpcClientImpl::GrpcClientImpl(ExtAuthzAsyncClientPtr&& async_client,
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

void GrpcClientImpl::check(RequestCallbacks& callbacks, const envoy::api::v2::auth::CheckRequest& request,
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

GrpcFactoryImpl::GrpcFactoryImpl(const std::string& cluster_name,
                                 Upstream::ClusterManager& cm)
    : cluster_name_(cluster_name), cm_(cm) {
  if (!cm_.get(cluster_name_)) {
    throw EnvoyException(fmt::format("unknown external authorization service cluster '{}'", cluster_name_));
  }
}

ClientPtr GrpcFactoryImpl::create(const Optional<std::chrono::milliseconds>& timeout) {
  return ClientPtr{new GrpcClientImpl(
      ExtAuthzAsyncClientPtr{
          new Grpc::AsyncClientImpl<envoy::api::v2::auth::CheckRequest,
                                    envoy::api::v2::auth::CheckResponse>(cm_, cluster_name_)},
      timeout)};
}

::envoy::api::v2::Address* CheckRequestGen::get_pbuf_address(const Network::Address::InstanceConstSharedPtr& instance) {
  using ::envoy::api::v2::Address;

  Address *addr = new Address();
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

::envoy::api::v2::auth::AttributeContext_Peer* CheckRequestGen::get_connection_peer(const Network::Connection *connection, const std::string& service, const bool local) {
  using ::envoy::api::v2::auth::AttributeContext_Peer;

  AttributeContext_Peer *peer = new AttributeContext_Peer();
  ASSERT(peer);

  // Set the address
  if (local == false) {
    peer->set_allocated_address(get_pbuf_address(connection->remoteAddress()));
  } else {
    peer->set_allocated_address(get_pbuf_address(connection->localAddress()));
  }

  // Set the principal
  // Preferably the SAN from the peer's cert or
  // Subject from the peer's cert.
  std::string principal;
  Ssl::Connection* ssl =
      const_cast<Ssl::Connection*>(connection->ssl());
  if (ssl != nullptr) {
    if (local == false) {
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

::envoy::api::v2::auth::AttributeContext_Peer* CheckRequestGen::get_connection_peer(const Network::Connection& connection, const std::string& service, const bool local) {
  return get_connection_peer(&connection, service, local);
}


const std::string CheckRequestGen::proto2str(const Envoy::Http::Protocol& p) {
  using Envoy::Http::Protocol;

  switch(p) {
    case Protocol::Http10:
      return std::string("Http1.0");
    case Protocol::Http11:
      return std::string("Http1.1");
    case Protocol::Http2:
      return std::string("Http2");
    default:
      break;
  }
  return std::string("unknown");
}

Envoy::Http::HeaderMap::Iterate CheckRequestGen::fill_http_headers(const Envoy::Http::HeaderEntry &e, void *ctx) {
  ::google::protobuf::Map< ::std::string, ::std::string >* mhdrs = static_cast<::google::protobuf::Map< ::std::string, ::std::string >*>(ctx);
  (*mhdrs)[std::string(e.key().c_str(), e.key().size())] = std::string(e.value().c_str(), e.value().size());
  return Envoy::Http::HeaderMap::Iterate::Continue;
}


::envoy::api::v2::auth::AttributeContext_Request* CheckRequestGen::get_http_request(const Envoy::Http::StreamDecoderFilterCallbacks* callbacks, const Envoy::Http::HeaderMap &headers) {
  using ::envoy::api::v2::auth::AttributeContext_Request;
  using ::envoy::api::v2::auth::AttributeContext_HTTPRequest;

  AttributeContext_HTTPRequest *httpreq = new AttributeContext_HTTPRequest();
  ASSERT(httpreq);

  // Set id
  // The streamId is not qualified as a const. Although it is as it does not modify the object.
  Envoy::Http::StreamDecoderFilterCallbacks *sdfc = const_cast<Envoy::Http::StreamDecoderFilterCallbacks *>(callbacks);
  httpreq->set_id(std::to_string(sdfc->streamId()));
  
  #define SET_HDR_IN_HTTPREQ(_hq, _api, mname)                          \
  do {                                                                  \
    const Envoy::Http::HeaderEntry *_entry = (_api)();                  \
    if (_entry != nullptr) {                                            \
      std::string _s(_entry->value().c_str(), _entry->value().size());  \
      (_hq)->set_##mname(_s);                                           \
    }                                                                   \
  } while(0)


  // Set method
  SET_HDR_IN_HTTPREQ(httpreq, headers.Method, method);
  // Set path
  SET_HDR_IN_HTTPREQ(httpreq, headers.Path, path);
  // Set host
  SET_HDR_IN_HTTPREQ(httpreq, headers.Host, host); 
  // Set scheme
  SET_HDR_IN_HTTPREQ(httpreq, headers.Scheme, scheme);

  // Set size
  // need to convert to google buffer 64t;
  httpreq->set_size(sdfc->requestInfo().bytesReceived());

  // Set protocol
  httpreq->set_protocol(proto2str(sdfc->requestInfo().protocol().value()));

  // Fill in the headers
  ::google::protobuf::Map< ::std::string, ::std::string >* mhdrs = httpreq->mutable_headers();
  headers.iterate(fill_http_headers, mhdrs);


  AttributeContext_Request *req = new AttributeContext_Request();
  ASSERT(req);
  req->set_allocated_http(httpreq);

  return req;
}

void CheckRequestGen::createHttpCheck(const Envoy::Http::StreamDecoderFilterCallbacks* callbacks, const Envoy::Http::HeaderMap &headers, envoy::api::v2::auth::CheckRequest& request) {
  using ::envoy::api::v2::auth::AttributeContext;

  AttributeContext* attrs = request.mutable_attributes();
  ASSERT(attrs);

  Envoy::Http::StreamDecoderFilterCallbacks* cb = const_cast<Envoy::Http::StreamDecoderFilterCallbacks *>(callbacks);

  std::string service;
  const Envoy::Http::HeaderEntry *entry = headers.EnvoyDownstreamServiceCluster();
  if (entry != nullptr) {
    service = std::string(entry->value().c_str(), entry->value().size());
  }
  attrs->set_allocated_source(get_connection_peer(cb->connection(), service, false));
  attrs->set_allocated_destination(get_connection_peer(cb->connection(), "", true));
  attrs->set_allocated_request(get_http_request(callbacks, headers));
}

void CheckRequestGen::createTcpCheck(const Network::ReadFilterCallbacks* callbacks, envoy::api::v2::auth::CheckRequest& request) {
  using ::envoy::api::v2::auth::AttributeContext;

  AttributeContext* attrs = request.mutable_attributes();
  ASSERT(attrs);

  Network::ReadFilterCallbacks* cb = const_cast<Network::ReadFilterCallbacks *>(callbacks);
  attrs->set_allocated_source(get_connection_peer(cb->connection(), "", false));
  attrs->set_allocated_destination(get_connection_peer(cb->connection(), "", true));
}

} // namespace ExtAuthz
} // namespace Envoy

