#include "extensions/filters/network/sip_proxy/router/router_impl.h"

#include <memory>

#include "envoy/extensions/filters/network/sip_proxy/v3/route.pb.h"
#include "envoy/upstream/cluster_manager.h"
#include "envoy/upstream/thread_local_cluster.h"

#include "common/network/address_impl.h"

#include "common/common/utility.h"
#include "common/router/metadatamatchcriteria_impl.h"

#include "extensions/filters/network/sip_proxy/app_exception_impl.h"
#include "extensions/filters/network/sip_proxy/encoder.h"
#include "extensions/filters/network/well_known_names.h"

#include "absl/strings/match.h"
#include <iostream>

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace Router {

RouteEntryImplBase::RouteEntryImplBase(
    const envoy::extensions::filters::network::sip_proxy::v3::Route& route)
    : cluster_name_(route.route().cluster()),
      config_headers_(Http::HeaderUtility::buildHeaderDataVector(route.match().headers())),
      // rate_limit_policy_(route.route().rate_limits()),
      strip_service_name_(route.route().strip_service_name()),
      cluster_header_(route.route().cluster_header()) {
  if (route.route().has_metadata_match()) {
    const auto filter_it = route.route().metadata_match().filter_metadata().find(
        Envoy::Config::MetadataFilters::get().ENVOY_LB);
    if (filter_it != route.route().metadata_match().filter_metadata().end()) {
      metadata_match_criteria_ =
          std::make_unique<Envoy::Router::MetadataMatchCriteriaImpl>(filter_it->second);
    }
  }
}

const std::string& RouteEntryImplBase::clusterName() const { return cluster_name_; }

const RouteEntry* RouteEntryImplBase::routeEntry() const { return this; }

RouteConstSharedPtr RouteEntryImplBase::clusterEntry(uint64_t random_value,
                                                     const MessageMetadata& metadata) const {
  (void)random_value;
  (void)metadata;
  return shared_from_this();
}

GeneralRouteEntryImpl::GeneralRouteEntryImpl(
    const envoy::extensions::filters::network::sip_proxy::v3::Route& route)
    : RouteEntryImplBase(route), domain_(route.match().domain()), invert_(route.match().invert()) {}

RouteConstSharedPtr GeneralRouteEntryImpl::matches(MessageMetadata& metadata,
                                                   uint64_t random_value) const {
  bool matches = metadata.domain().value() == domain_ || domain_ == "*";

  if (matches ^ invert_) {
    return clusterEntry(random_value, metadata);
  }

  return nullptr;
}

RouteMatcher::RouteMatcher(
    const envoy::extensions::filters::network::sip_proxy::v3::RouteConfiguration& config) {
  using envoy::extensions::filters::network::sip_proxy::v3::RouteMatch;

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

RouteConstSharedPtr RouteMatcher::route(MessageMetadata& metadata, uint64_t random_value) const {
  for (const auto& route : routes_) {
    RouteConstSharedPtr route_entry = route->matches(metadata, random_value);
    if (nullptr != route_entry) {
      return route_entry;
    }
  }

  return nullptr;
}

void Router::onDestroy() {
  if (!callbacks_->transactionId().empty())
    for (auto& kv : *transaction_infos_) {
      auto transaction_info = kv.second;
      try {
        transaction_info->getTransaction(callbacks_->transactionId());
        transaction_info->deleteTransaction(callbacks_->transactionId());
      } catch (std::out_of_range) {
      }
    }
  //  if (upstream_request_ != nullptr) {
  //    upstream_request_->resetStream();
  //    cleanup();
  //  }
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

FilterStatus Router::transportEnd() {
  //  if (upstream_request_->metadata_->messageType() == MessageType::Oneway) {
  //    // No response expected
  //    upstream_request_->onResponseComplete();
  //    cleanup();
  //  }
  return FilterStatus::Continue;
}

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
      cluster_->extensionProtocolOptionsTyped<ProtocolOptionsConfig>(
          NetworkFilterNames::get().SipProxy);

  auto& transaction_info = (*transaction_infos_)[cluster_name];

  auto message_handler_with_loadbalancer = [&]() {
    Tcp::ConnectionPool::Instance* conn_pool = cluster->tcpConnPool(Upstream::ResourcePriority::Default, this);
    if (!conn_pool) {
      stats_.no_healthy_upstream_.inc();
      callbacks_->sendLocalReply(
          AppException(AppExceptionType::InternalError,
                       fmt::format("no healthy upstream for '{}'", cluster_name)),
          true);
      return FilterStatus::StopIteration;
    }

    ENVOY_STREAM_LOG(debug, "router decoding request", *callbacks_);

    Upstream::HostDescriptionConstSharedPtr host = conn_pool->host();
    if (!host) {
      return FilterStatus::StopIteration;
    }

    if (auto upstream_request = transaction_info->getUpstreamRequest(host->address()->asString());
        upstream_request != nullptr) {
      // There is action connection, reuse it.
      upstream_request_ = upstream_request;
      upstream_request_->setDecoderFilterCallbacks(*callbacks_);
      ENVOY_STREAM_LOG(debug, "reuse upstream request", *callbacks_);
      try {
        transaction_info->getTransaction(std::string(metadata->transactionId().value()));
      } catch (std::out_of_range) {
        transaction_info->insertTransaction(std::string(metadata->transactionId().value()),
                                            callbacks_, upstream_request_);
      }
    } else {
      upstream_request_ = std::make_shared<UpstreamRequest>(*conn_pool, transaction_info);
      upstream_request_->setDecoderFilterCallbacks(*callbacks_);
      transaction_info->insertUpstreamRequest(host->address()->asString(), upstream_request_);
      ENVOY_STREAM_LOG(debug, "create new upstream request", *callbacks_);

      try {
        transaction_info->getTransaction(std::string(metadata->transactionId().value()));
      } catch (std::out_of_range) {
        transaction_info->insertTransaction(std::string(metadata->transactionId().value()),
                                            callbacks_, upstream_request_);
      }
    }
    return upstream_request_->start();
  };

  switch (metadata->methodType()) {
  case MethodType::Invite: {
    message_handler_with_loadbalancer();
    break;
  }
  case MethodType::Ack: { // ACK_200
    auto& transaction_info = (*transaction_infos_)[cluster_name];
    if (settings_->sessionStickness()) {
      ENVOY_LOG(trace, "session stickness is true, choose ep");
      auto host = metadata->EP().value();
      if (auto upstream_request = transaction_info->getUpstreamRequest(std::string(host));
          upstream_request != nullptr) {
        // There is action connection, reuse it.
        upstream_request_ = upstream_request;
#if 1
        try {
          transaction_info->getTransaction(std::string(metadata->transactionId().value()));
        } catch (std::out_of_range) {
          transaction_info->insertTransaction(std::string(metadata->transactionId().value()),
                                              callbacks_, upstream_request_);
        }
#endif
        return upstream_request_->start();
      } else {
        message_handler_with_loadbalancer();
      }
    } else {
      message_handler_with_loadbalancer();
    }
    break;
  }
  default:
    auto& transaction_info = (*transaction_infos_)[cluster_name];
    try {
      auto active_trans =
          transaction_info->getTransaction(std::string(metadata->transactionId().value()));

      upstream_request_ = active_trans.upstreamRequest();
      return FilterStatus::Continue;
    } catch (std::out_of_range) {
      if (settings_->sessionStickness()) {
        ENVOY_LOG(trace, "session stickness is true, choose ep");
        auto host = metadata->EP().value();
        if (auto upstream_request = transaction_info->getUpstreamRequest(std::string(host));
            upstream_request != nullptr) {
          // There is action connection, reuse it.
          upstream_request_ = upstream_request;

          try {
            transaction_info->getTransaction(std::string(metadata->transactionId().value()));
          } catch (std::out_of_range) {
            transaction_info->insertTransaction(std::string(metadata->transactionId().value()),
                                                callbacks_, upstream_request_);
          }

          return upstream_request_->start();
        } else {
          message_handler_with_loadbalancer();
        }
      } else {
        message_handler_with_loadbalancer();
      }
    }
    break;
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

  metadata_->setEP(upstream_request_->localAddress());
  std::shared_ptr<Encoder> encoder = std::make_shared<EncoderImpl>();
  encoder->encode(metadata_, transport_buffer);

  // static size_t counter = 0;
  ENVOY_STREAM_LOG(trace, "send buffer : {} bytes\n{}", *callbacks_, transport_buffer.length(),
                   transport_buffer.toString());

  //  upstream_request_->transport_->encodeFrame(transport_buffer, *upstream_request_->metadata_,
  //                                             upstream_request_buffer_);
  upstream_request_->write(transport_buffer, false);
  //upstream_request_->onRequestComplete();
  return FilterStatus::Continue;
}

const Network::Connection* Router::downstreamConnection() const {
  if (callbacks_ != nullptr) {
    return callbacks_->connection();
  }

  return nullptr;
}

void Router::cleanup() { /*upstream_request_.reset();*/
}

UpstreamRequest::UpstreamRequest(Tcp::ConnectionPool::Instance& pool,
                                 std::shared_ptr<TransactionInfo> transaction_info)
    : conn_pool_(pool), transaction_info_(transaction_info), /*request_complete_(false),*/
      /*response_started_(false),*/ response_complete_(false) {}

UpstreamRequest::~UpstreamRequest() {
  if (conn_pool_handle_) {
    conn_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
  }
}

FilterStatus UpstreamRequest::start() {
  if (conn_state_ != ConnectionState::NotConnected) {
    return FilterStatus::Continue;
  }

  ENVOY_LOG(info, "connecting {}", conn_pool_.host()->address()->asString());
  conn_state_ = ConnectionState::Connecting;

  Tcp::ConnectionPool::Cancellable* handle = conn_pool_.newConnection(*this);
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

  conn_state_ = ConnectionState::NotConnected;

  // The event triggered by close will also release this connection so clear conn_data_ before
  // closing.
  auto conn_data = std::move(conn_data_);
  if (close && conn_data != nullptr) {
    conn_data->connection().close(Network::ConnectionCloseType::NoFlush);
  }
}

void UpstreamRequest::resetStream() { releaseConnection(true); }

void UpstreamRequest::onPoolFailure(ConnectionPool::PoolFailureReason reason,
                                    Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(info, "on pool failure");
  conn_state_ = ConnectionState::NotConnected;
  conn_pool_handle_ = nullptr;

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
  UNREFERENCED_PARAMETER(reason);
  //onResetStream(reason);
}

void UpstreamRequest::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                  Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_STREAM_LOG(trace, "onPoolReady", *callbacks_);

  // Only invoke continueDecoding if we'd previously stopped the filter chain.
  bool continue_decoding = conn_pool_handle_ != nullptr;

  conn_data_ = std::move(conn_data);

  onUpstreamHostSelected(host);
  conn_data_->addUpstreamCallbacks(*this);
  conn_pool_handle_ = nullptr;

  conn_state_ = ConnectionState::Connected;

  onRequestStart(continue_decoding);
}

void UpstreamRequest::onRequestStart(bool continue_decoding) {
  if (!pending_request_.empty()) {
    for (const auto& metadata : pending_request_) {
      Buffer::OwnedImpl transport_buffer;

      metadata->setEP(localAddress());
      std::shared_ptr<Encoder> encoder = std::make_shared<EncoderImpl>();
      encoder->encode(metadata, transport_buffer);

      ENVOY_STREAM_LOG(trace, "send buffer : {} bytes\n{}", *callbacks_, transport_buffer.length(),
                       transport_buffer.toString());
      conn_data_->connection().write(transport_buffer, false);
      //onRequestComplete();
    }
    pending_request_.clear();
  }
  UNREFERENCED_PARAMETER(continue_decoding);
  // if (continue_decoding) {
  //  callbacks_->continueDecoding();
  //}
}

//void UpstreamRequest::onRequestComplete() { request_complete_ = true; }

//void UpstreamRequest::onResponseComplete() {
//  response_complete_ = true;
//  conn_state_ = nullptr;
//  conn_data_.reset();
//}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  upstream_host_ = host;
}

void UpstreamRequest::onResetStream(ConnectionPool::PoolFailureReason reason) {
  // if (metadata_->messageType() == MessageType::Oneway) {
  //  // For oneway requests, we should not attempt a response. Reset the downstream to signal
  //  // an error.
  //  parent_.callbacks_->resetDownstreamConnection();
  //  return;
  //}

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
    //if (!response_started_) {
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
  } catch (std::out_of_range) {
    return nullptr;
  }
}

// Tcp::ConnectionPool::UpstreamCallbacks
void UpstreamRequest::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  UNREFERENCED_PARAMETER(end_stream);
  upstream_buffer_.move(data);
  // ASSERT(!upstream_request_->response_complete_);

  // ENVOY_STREAM_LOG(trace, "reading response: {} bytes", *callbacks_, data.length());

  // std::cout << "received response \n" << data.toString() << std::endl;
  auto response_decoder_ = std::make_unique<ResponseDecoder>(*this);
  response_decoder_->onData(upstream_buffer_);
  // std::cout << "received response left " << data.length() << std::endl;

  //      auto parser = std::make_shared<SipParser>(data.toString());
  //      parser->decode();
  //
  //      auto callbacks = parent_.transaction_map_[parser.->metadata()->transactionId];
  //
  //      request_ = std::make_unique<ActiveRequest>(callbacks_.newDecoderEventHandler());
  //      request_->handler_.run(parser);
  //
  //
  //      // Handle normal response.
  //      if (!upstream_request_->response_started_) {
  //        callbacks->startUpstreamResponse();
  //        upstream_request_->response_started_ = true;
  //      }
  //
  //      SipFilters::ResponseStatus status = callbacks->upstreamData(data);
  //      if (status == SipFilters::ResponseStatus::Complete) {
  //        ENVOY_STREAM_LOG(debug, "response complete", *callbacks_);
  //        upstream_request_->onResponseComplete();
  //        ENVOY_STREAM_LOG(debug, "response complete", *callbacks_);
  //        cleanup();
  //        return;
  //      } else if (status == SipFilters::ResponseStatus::Reset) {
  //        ENVOY_STREAM_LOG(debug, "upstream reset", *callbacks_);
  //        upstream_request_->resetStream();
  //        return;
  //      }
  //
  //      if (end_stream) {
  //        // Response is incomplete, but no more data is coming.
  //        ENVOY_STREAM_LOG(debug, "response underflow", *callbacks_);
  //        upstream_request_->onResponseComplete();
  //        upstream_request_->onResetStream(
  //            ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
  //        cleanup();
  //      }
}
void UpstreamRequest::onEvent(Network::ConnectionEvent event) {
  ENVOY_LOG(info, "received upstream event {}", event);
  switch (event) {
  case Network::ConnectionEvent::RemoteClose:
    ENVOY_STREAM_LOG(debug, "upstream remote close", *callbacks_);
    //onResetStream(ConnectionPool::PoolFailureReason::RemoteConnectionFailure);
    break;
  case Network::ConnectionEvent::LocalClose:
    ENVOY_STREAM_LOG(debug, "upstream local close", *callbacks_);
    //onResetStream(ConnectionPool::PoolFailureReason::LocalConnectionFailure);
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
  // std::cout << "There are still " << upstream_buffer_.length() << " in upstream_buffer_" <<
  // std::endl;
  return true;
}

FilterStatus ResponseDecoder::transportBegin(MessageMetadataSharedPtr metadata) {
  // ENVOY_LOG(trace, "ResponseDecoder {metadata->rawMsg()}");
  if (metadata->transactionId().has_value()) {
    auto transaction_id = metadata->transactionId().value();

    auto active_trans = parent_.getTransaction(std::string(transaction_id));
    if (active_trans) {
      active_trans->startUpstreamResponse();
      active_trans->upstreamData(metadata);
    } else {
      std::cout << "no active trans selected " << transaction_id << "\n>>>>>>>>>>>>>>>>>>>\n"
                << metadata->rawMsg() << "\n<<<<<<<<<<<<<<<<<<<<<" << std::endl;
      return FilterStatus::StopIteration;
    }
  } else {
    std::cout << ">>>>>>>>>>>>>>>>>>>\n"
              << metadata->rawMsg() << "\n<<<<<<<<<<<<<<<<<<<<<" << std::endl;
    return FilterStatus::StopIteration;
  }

  return FilterStatus::Continue;
}
} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
