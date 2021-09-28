#include "contrib/sip_proxy/filters/network/source/router/router_impl.h"

#include <memory>

#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/router/metadatamatchcriteria_impl.h"

#include "absl/strings/match.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/route.pb.h"
#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"
#include "contrib/sip_proxy/filters/network/source/encoder.h"
#include "contrib/sip_proxy/filters/network/source/filters/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace Router {

RouteEntryImplBase::RouteEntryImplBase(
    const envoy::extensions::filters::network::sip_proxy::v3alpha::Route& route)
    : cluster_name_(route.route().cluster()) {}

const std::string& RouteEntryImplBase::clusterName() const { return cluster_name_; }

const RouteEntry* RouteEntryImplBase::routeEntry() const { return this; }

RouteConstSharedPtr RouteEntryImplBase::clusterEntry(const MessageMetadata& metadata) const {
  UNREFERENCED_PARAMETER(metadata);
  return shared_from_this();
}

GeneralRouteEntryImpl::GeneralRouteEntryImpl(
    const envoy::extensions::filters::network::sip_proxy::v3alpha::Route& route)
    : RouteEntryImplBase(route), domain_(route.match().domain()) {}

RouteConstSharedPtr GeneralRouteEntryImpl::matches(MessageMetadata& metadata) const {
  bool matches = metadata.domain().value() == domain_ || domain_ == "*";

  if (matches) {
    return clusterEntry(metadata);
  }

  return nullptr;
}

RouteMatcher::RouteMatcher(
    const envoy::extensions::filters::network::sip_proxy::v3alpha::RouteConfiguration& config) {
  using envoy::extensions::filters::network::sip_proxy::v3alpha::RouteMatch;

  for (const auto& route : config.routes()) {
    switch (route.match().match_specifier_case()) {
    case RouteMatch::MatchSpecifierCase::kDomain:
      routes_.emplace_back(new GeneralRouteEntryImpl(route));
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
}

RouteConstSharedPtr RouteMatcher::route(MessageMetadata& metadata) const {
  for (const auto& route : routes_) {
    RouteConstSharedPtr route_entry = route->matches(metadata);
    if (nullptr != route_entry) {
      return route_entry;
    }
  }

  return nullptr;
}

void Router::onDestroy() {
  if (!callbacks_->transactionId().empty()) {
    for (auto& kv : *transaction_infos_) {
      auto transaction_info = kv.second;
      try {
        transaction_info->getTransaction(callbacks_->transactionId());
        transaction_info->deleteTransaction(callbacks_->transactionId());
      } catch (std::out_of_range const&) {
      }
    }
  }
}

void Router::setDecoderFilterCallbacks(SipFilters::DecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
  transaction_infos_ = callbacks_->transactionInfos();
  settings_ = callbacks_->settings();
}

FilterStatus Router::transportBegin(MessageMetadataSharedPtr metadata) {
  UNREFERENCED_PARAMETER(metadata);
  return FilterStatus::Continue;
}

FilterStatus Router::transportEnd() { return FilterStatus::Continue; }

FilterStatus Router::messageBegin(MessageMetadataSharedPtr metadata) {
  if (upstream_request_ != nullptr) {
    return FilterStatus::Continue;
  }

  metadata_ = metadata;
  route_ = callbacks_->route();
  if (!route_) {
    ENVOY_STREAM_LOG(debug, "no route match domain {}", *callbacks_, metadata->domain().value());
    stats_.route_missing_.inc();
    callbacks_->sendLocalReply(AppException(AppExceptionType::UnknownMethod, "no route for method"),
                               true);
    return FilterStatus::StopIteration;
  }

  route_entry_ = route_->routeEntry();
  const std::string& cluster_name = route_entry_->clusterName();

  Upstream::ThreadLocalCluster* cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (!cluster) {
    ENVOY_STREAM_LOG(debug, "unknown cluster '{}'", *callbacks_, cluster_name);
    stats_.unknown_cluster_.inc();
    callbacks_->sendLocalReply(AppException(AppExceptionType::InternalError,
                                            fmt::format("unknown cluster '{}'", cluster_name)),
                               true);
    return FilterStatus::StopIteration;
  }

  cluster_ = cluster->info();
  ENVOY_STREAM_LOG(debug, "cluster '{}' match domain {}", *callbacks_, cluster_name,
                   std::string(metadata->domain().value()));

  if (cluster_->maintenanceMode()) {
    stats_.upstream_rq_maintenance_mode_.inc();
    callbacks_->sendLocalReply(
        AppException(AppExceptionType::InternalError,
                     fmt::format("maintenance mode for cluster '{}'", cluster_name)),
        true);
    return FilterStatus::StopIteration;
  }

  const std::shared_ptr<const ProtocolOptionsConfig> options =
      cluster_->extensionProtocolOptionsTyped<ProtocolOptionsConfig>(SipProxy);

  auto handle_affinity = [&](const std::shared_ptr<const ProtocolOptionsConfig> options) {
    if (options == nullptr || metadata->msgType() == MsgType::Response) {
      return;
    }

    if (metadata->methodType() != MethodType::Register && options->sessionAffinity()) {
      if (metadata->routeEP().has_value()) {
        auto host = metadata->routeEP().value();
        metadata->setDestination(host);
      }
    }
    if (metadata->methodType() == MethodType::Register && options->registrationAffinity()) {
      if (metadata->routeOpaque().has_value()) {
        auto host = metadata->routeOpaque().value();
        metadata->setDestination(host);
      }
    }
  };
  handle_affinity(options);

  auto& transaction_info = (*transaction_infos_)[cluster_name];

  auto message_handler_with_loadbalancer = [&]() {
    auto pool_data = cluster->tcpConnPool(Upstream::ResourcePriority::Default, this);
    if (!pool_data) {
      stats_.no_healthy_upstream_.inc();
      callbacks_->sendLocalReply(
          AppException(AppExceptionType::InternalError,
                       fmt::format("no healthy upstream for '{}'", cluster_name)),
          true);
      return FilterStatus::StopIteration;
    }

    ENVOY_STREAM_LOG(debug, "router decoding request", *callbacks_);

    Upstream::HostDescriptionConstSharedPtr host = pool_data->host();
    if (!host) {
      return FilterStatus::StopIteration;
    }

    if (auto upstream_request =
            transaction_info->getUpstreamRequest(host->address()->ip()->addressAsString());
        upstream_request != nullptr) {
      // There is action connection, reuse it.
      upstream_request_ = upstream_request;
      upstream_request_->setDecoderFilterCallbacks(*callbacks_);
      ENVOY_STREAM_LOG(debug, "reuse upstream request", *callbacks_);
      try {
        transaction_info->getTransaction(std::string(metadata->transactionId().value()));
      } catch (std::out_of_range const&) {
        transaction_info->insertTransaction(std::string(metadata->transactionId().value()),
                                            callbacks_, upstream_request_);
      }
    } else {
      upstream_request_ = std::make_shared<UpstreamRequest>(*pool_data, transaction_info);
      upstream_request_->setDecoderFilterCallbacks(*callbacks_);
      transaction_info->insertUpstreamRequest(host->address()->ip()->addressAsString(),
                                              upstream_request_);
      ENVOY_STREAM_LOG(debug, "create new upstream request {}", *callbacks_,
                       host->address()->ip()->addressAsString());

      try {
        transaction_info->getTransaction(std::string(metadata->transactionId().value()));
      } catch (std::out_of_range const&) {
        transaction_info->insertTransaction(std::string(metadata->transactionId().value()),
                                            callbacks_, upstream_request_);
      }
    }
    return upstream_request_->start();
  };

  if (metadata->destination().has_value()) {
    auto host = metadata->destination().value();
    if (auto upstream_request = transaction_info->getUpstreamRequest(std::string(host));
        upstream_request != nullptr) {
      // There is action connection, reuse it.
      ENVOY_STREAM_LOG(debug, "reuse upstream request from EP {}", *callbacks_, host);
      upstream_request_ = upstream_request;

      try {
        transaction_info->getTransaction(std::string(metadata->transactionId().value()));
      } catch (std::out_of_range const&) {
        transaction_info->insertTransaction(std::string(metadata->transactionId().value()),
                                            callbacks_, upstream_request_);
      }
      return upstream_request_->start();
    } else {
      ENVOY_STREAM_LOG(debug, "get upstream request for {} failed.", *callbacks_, host);
      message_handler_with_loadbalancer();
    }
  } else {
    ENVOY_STREAM_LOG(debug, "no destination.", *callbacks_);
    message_handler_with_loadbalancer();
  }

  return FilterStatus::Continue;
}

FilterStatus Router::messageEnd() {
  // In case pool is not ready, save this into pending_request.
  if (upstream_request_->connectionState() != ConnectionState::Connected) {
    upstream_request_->addIntoPendingRequest(metadata_);
    return FilterStatus::Continue;
  }

  Buffer::OwnedImpl transport_buffer;

  // set EP/Opaque, used in upstream
  metadata_->setEP(upstream_request_->getLocalIp());

  std::shared_ptr<Encoder> encoder = std::make_shared<EncoderImpl>();
  encoder->encode(metadata_, transport_buffer);

  ENVOY_STREAM_LOG(trace, "send buffer : {} bytes\n{}", *callbacks_, transport_buffer.length(),
                   transport_buffer.toString());

  upstream_request_->write(transport_buffer, false);
  return FilterStatus::Continue;
}

const Network::Connection* Router::downstreamConnection() const {
  if (callbacks_ != nullptr) {
    return callbacks_->connection();
  }

  return nullptr;
}

void Router::cleanup() { upstream_request_.reset(); }

UpstreamRequest::UpstreamRequest(Upstream::TcpPoolData& pool_data,
                                 std::shared_ptr<TransactionInfo> transaction_info)
    : conn_pool_data_(pool_data), transaction_info_(transaction_info), response_complete_(false) {}

UpstreamRequest::~UpstreamRequest() {
  if (conn_pool_handle_) {
    conn_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
  }
}

FilterStatus UpstreamRequest::start() {
  if (connectionState() != ConnectionState::NotConnected) {
    return FilterStatus::Continue;
  }

  ENVOY_LOG(info, "connecting {}", conn_pool_data_.host()->address()->asString());

  setConnectionState(ConnectionState::Connecting);
  conn_state_ = ConnectionState::Connecting;

  Tcp::ConnectionPool::Cancellable* handle = conn_pool_data_.newConnection(*this);
  if (handle) {
    // Pause while we wait for a connection.
    conn_pool_handle_ = handle;
    return FilterStatus::Continue;
  }

  if (upstream_host_ == nullptr) {
    return FilterStatus::StopIteration;
  }

  return FilterStatus::Continue;
}

void UpstreamRequest::releaseConnection(const bool close) {
  if (conn_pool_handle_) {
    conn_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
    conn_pool_handle_ = nullptr;
  }

  setConnectionState(ConnectionState::NotConnected);

  // The event triggered by close will also release this connection so clear conn_data_ before
  // closing.
  auto conn_data = std::move(conn_data_);
  if (close && conn_data != nullptr) {
    conn_data->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

void UpstreamRequest::resetStream() { releaseConnection(true); }

void UpstreamRequest::onPoolFailure(ConnectionPool::PoolFailureReason reason, absl::string_view,
                                    Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(info, "on pool failure");
  setConnectionState(ConnectionState::NotConnected);
  conn_pool_handle_ = nullptr;

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  UNREFERENCED_PARAMETER(reason);
}

void UpstreamRequest::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                  Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_STREAM_LOG(trace, "onPoolReady", *callbacks_);

  conn_data_ = std::move(conn_data);

  onUpstreamHostSelected(host);
  conn_data_->addUpstreamCallbacks(*this);
  conn_pool_handle_ = nullptr;

  setConnectionState(ConnectionState::Connected);

  onRequestStart();
}

void UpstreamRequest::onRequestStart() {
  if (!pending_request_.empty()) {
    for (const auto& metadata : pending_request_) {
      Buffer::OwnedImpl transport_buffer;

      // set EP/Opaque, used in upstream
      metadata->setEP(getLocalIp());

      std::shared_ptr<Encoder> encoder = std::make_shared<EncoderImpl>();
      encoder->encode(metadata, transport_buffer);

      ENVOY_STREAM_LOG(trace, "send buffer : {} bytes\n{}", *callbacks_, transport_buffer.length(),
                       transport_buffer.toString());
      conn_data_->connection().write(transport_buffer, false);
    }
    pending_request_.clear();
  }
}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  upstream_host_ = host;
}

void UpstreamRequest::onResetStream(ConnectionPool::PoolFailureReason reason) {
  switch (reason) {
  case ConnectionPool::PoolFailureReason::Overflow:
    callbacks_->sendLocalReply(
        AppException(AppExceptionType::InternalError, "sip upstream request: too many connections"),
        true);
    break;
  case ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    // Should only happen if we closed the connection, due to an error condition, in which case
    // we've already handled any possible downstream response.
    callbacks_->resetDownstreamConnection();
    break;
  case ConnectionPool::PoolFailureReason::RemoteConnectionFailure:
  case ConnectionPool::PoolFailureReason::Timeout:
    // TODO(zuercher): distinguish between these cases where appropriate (particularly timeout)
    // if (!response_started_) {
    //  callbacks_->sendLocalReply(
    //      AppException(
    //          AppExceptionType::InternalError,
    //          fmt::format("connection failure '{}'", (upstream_host_ != nullptr)
    //                                                     ? upstream_host_->address()->asString()
    //                                                     : "to upstream")),
    //      true);
    //  return;
    //}

    // Error occurred after a partial response, propagate the reset to the downstream.
    callbacks_->resetDownstreamConnection();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

SipFilters::DecoderFilterCallbacks* UpstreamRequest::getTransaction(std::string&& transaction_id) {
  try {
    return transaction_info_->getTransaction(std::move(transaction_id)).activeTrans();
  } catch (std::out_of_range const&) {
    return nullptr;
  }
}

// Tcp::ConnectionPool::UpstreamCallbacks
void UpstreamRequest::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  UNREFERENCED_PARAMETER(end_stream);
  upstream_buffer_.move(data);
  auto response_decoder_ = std::make_unique<ResponseDecoder>(*this);
  response_decoder_->onData(upstream_buffer_);
}

void UpstreamRequest::onEvent(Network::ConnectionEvent event) {
  ENVOY_LOG(info, "received upstream event {}", event);
  switch (event) {
  case Network::ConnectionEvent::RemoteClose:
    ENVOY_STREAM_LOG(debug, "upstream remote close", *callbacks_);
    break;
  case Network::ConnectionEvent::LocalClose:
    ENVOY_STREAM_LOG(debug, "upstream local close", *callbacks_);
    break;
  default:
    // Connected is consumed by the connection pool.
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  releaseConnection(false);
}

void UpstreamRequest::setDecoderFilterCallbacks(SipFilters::DecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

bool ResponseDecoder::onData(Buffer::Instance& data) {
  decoder_->onData(data);
  return true;
}

FilterStatus ResponseDecoder::transportBegin(MessageMetadataSharedPtr metadata) {
  ENVOY_LOG(trace, "ResponseDecoder {}", metadata->rawMsg());
  if (metadata->transactionId().has_value()) {
    auto transaction_id = metadata->transactionId().value();

    auto active_trans = parent_.getTransaction(std::string(transaction_id));
    if (active_trans) {
      active_trans->startUpstreamResponse();
      active_trans->upstreamData(metadata);
    } else {
      ENVOY_LOG(debug, "no active trans selected {}\n{}", transaction_id, metadata->rawMsg());
      return FilterStatus::StopIteration;
    }
  } else {
    ENVOY_LOG(debug, "no active trans selected \n{}", metadata->rawMsg());
    return FilterStatus::StopIteration;
  }

  return FilterStatus::Continue;
}

absl::string_view ResponseDecoder::getLocalIp() { return parent_.getLocalIp(); }

std::string ResponseDecoder::getOwnDomain() { return parent_.transactionInfo()->getOwnDomain(); }

std::string ResponseDecoder::getDomainMatchParamName() {
  return parent_.transactionInfo()->getDomainMatchParamName();
}

} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
