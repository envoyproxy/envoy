#include "contrib/sip_proxy/filters/network/source/tra/tra_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/grpc_service.pb.h"
#include "envoy/stats/scope.h"

#include "source/common/common/assert.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace TrafficRoutingAssistant {

GrpcClientImpl::GrpcClientImpl(const Grpc::RawAsyncClientSharedPtr& async_client,
                               Event::Dispatcher& dispatcher,
                               const absl::optional<std::chrono::milliseconds>& timeout)
    : async_client_(async_client), dispatcher_(dispatcher), timeout_(timeout) {}

GrpcClientImpl::~GrpcClientImpl() {
  while (!request_callbacks_.empty()) {
    request_callbacks_.front()->request_->cancel();
    request_callbacks_.front()->cleanup();
  }

  while (!stream_callbacks_.empty()) {
    stream_callbacks_.front()->stream_.resetStream();
    stream_callbacks_.front()->cleanup();
  }
}

void GrpcClientImpl::setRequestCallbacks(RequestCallbacks& callbacks) {
  // ASSERT(callbacks_ == nullptr);
  callbacks_ = &callbacks;
}

void GrpcClientImpl::createTrafficRoutingAssistant(
    const std::string& type, const absl::flat_hash_map<std::string, std::string>& data,
    Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info) {

  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceRequest request;
  request.set_type(type);

  for (auto& item : data) {
    (*request.mutable_create_request()->mutable_data())[item.first] = item.second;
  }

  const auto& service_method = *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      "envoy.extensions.filters.network.sip_proxy.tra.v3alpha.TraService.Create");

  std::unique_ptr<AsyncRequestCallbacks> callback = std::make_unique<AsyncRequestCallbacks>(*this);
  callback->request_ =
      async_client_->send(service_method, request, *callback, parent_span,
                          Http::AsyncClient::RequestOptions().setTimeout(timeout_).setParentContext(
                              Http::AsyncClient::ParentContext{&stream_info}));
  LinkedList::moveIntoList(std::move(callback), request_callbacks_);
}

void GrpcClientImpl::updateTrafficRoutingAssistant(
    const std::string& type, const absl::flat_hash_map<std::string, std::string>& data,
    Tracing::Span& parent_span, const StreamInfo::StreamInfo& stream_info) {
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceRequest request;
  request.set_type(type);

  for (auto& item : data) {
    (*request.mutable_update_request()->mutable_data())[item.first] = item.second;
  }

  const auto& service_method = *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      "envoy.extensions.filters.network.sip_proxy.tra.v3alpha.TraService.Update");
  std::unique_ptr<AsyncRequestCallbacks> callback = std::make_unique<AsyncRequestCallbacks>(*this);
  callback->request_ =
      async_client_->send(service_method, request, *callback, parent_span,
                          Http::AsyncClient::RequestOptions().setTimeout(timeout_).setParentContext(
                              Http::AsyncClient::ParentContext{&stream_info}));
  LinkedList::moveIntoList(std::move(callback), request_callbacks_);
}

void GrpcClientImpl::retrieveTrafficRoutingAssistant(const std::string& type,
                                                     const std::string& key,
                                                     Tracing::Span& parent_span,
                                                     const StreamInfo::StreamInfo& stream_info) {

  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceRequest request;
  request.set_type(type);
  request.mutable_retrieve_request()->set_key(key);

  const auto& service_method = *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      "envoy.extensions.filters.network.sip_proxy.tra.v3alpha.TraService.Retrieve");
  std::unique_ptr<AsyncRequestCallbacks> callback = std::make_unique<AsyncRequestCallbacks>(*this);
  callback->request_ =
      async_client_->send(service_method, request, *callback, parent_span,
                          Http::AsyncClient::RequestOptions().setTimeout(timeout_).setParentContext(
                              Http::AsyncClient::ParentContext{&stream_info}));
  LinkedList::moveIntoList(std::move(callback), request_callbacks_);
}

void GrpcClientImpl::deleteTrafficRoutingAssistant(const std::string& type, const std::string& key,
                                                   Tracing::Span& parent_span,
                                                   const StreamInfo::StreamInfo& stream_info) {

  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceRequest request;
  request.set_type(type);
  request.mutable_delete_request()->set_key(key);

  const auto& service_method = *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      "envoy.extensions.filters.network.sip_proxy.tra.v3alpha.TraService.Delete");
  std::unique_ptr<AsyncRequestCallbacks> callback = std::make_unique<AsyncRequestCallbacks>(*this);
  callback->request_ =
      async_client_->send(service_method, request, *callback, parent_span,
                          Http::AsyncClient::RequestOptions().setTimeout(timeout_).setParentContext(
                              Http::AsyncClient::ParentContext{&stream_info}));
  LinkedList::moveIntoList(std::move(callback), request_callbacks_);
}

void GrpcClientImpl::subscribeTrafficRoutingAssistant(const std::string& type,
                                                      Tracing::Span& parent_span,
                                                      const StreamInfo::StreamInfo& stream_info) {
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceRequest request;

  UNREFERENCED_PARAMETER(parent_span);
  request.set_type(type);
  request.mutable_subscribe_request();

  const auto& service_method = *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(
      "envoy.extensions.filters.network.sip_proxy.tra.v3alpha.TraService.Subscribe");
  std::unique_ptr<AsyncStreamCallbacks> callback = std::make_unique<AsyncStreamCallbacks>(*this);
  callback->stream_ = async_client_->start(service_method, *callback,
                                           Http::AsyncClient::StreamOptions().setParentContext(
                                               Http::AsyncClient::ParentContext{&stream_info}));
  callback->stream_.sendMessage(request, false);
  LinkedList::moveIntoList(std::move(callback), stream_callbacks_);
}

void GrpcClientImpl::onSuccess(
    std::unique_ptr<
        envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>&&
        response,
    Tracing::Span& span) {

  UNREFERENCED_PARAMETER(span);
  if (response->has_create_response()) {
    callbacks_->complete(ResponseType::CreateResp, response->type(), response->create_response());
  } else if (response->has_update_response()) {
    callbacks_->complete(ResponseType::UpdateResp, response->type(), response->update_response());
  } else if (response->has_retrieve_response()) {
    callbacks_->complete(ResponseType::RetrieveResp, response->type(),
                         response->retrieve_response());
  } else if (response->has_delete_response()) {
    callbacks_->complete(ResponseType::DeleteResp, response->type(), response->delete_response());
  }
  // callbacks_ = nullptr;
}

void GrpcClientImpl::onFailure(Grpc::Status::GrpcStatus status, const std::string& message,
                               Tracing::Span&) {
  ASSERT(status != Grpc::Status::WellKnownGrpcStatus::Ok);
  ENVOY_LOG(error, "GrpcClientImpl Failure {} {}", message, status);
  // callbacks_->complete(ResponseType::FailureResp, status);
  // callbacks_ = nullptr;
}

void GrpcClientImpl::onReceiveMessage(
    std::unique_ptr<
        envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceResponse>&&
        message) {
  callbacks_->complete(ResponseType::SubscribeResp, message->type(), message->subscribe_response());
  // callbacks_ = nullptr;
}

ClientPtr traClient(Event::Dispatcher& dispatcher, Server::Configuration::FactoryContext& context,
                    const envoy::config::core::v3::GrpcService& grpc_service,
                    const std::chrono::milliseconds timeout) {
  // TODO(ramaraochavali): register client to singleton when GrpcClientImpl supports concurrent
  // requests.
  return std::make_unique<SipProxy::TrafficRoutingAssistant::GrpcClientImpl>(
      context.clusterManager().grpcAsyncClientManager().getOrCreateRawAsyncClient(
          grpc_service, context.scope(), true, Grpc::CacheOption::CacheWhenRuntimeEnabled),
      dispatcher, timeout);
}

} // namespace TrafficRoutingAssistant
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
