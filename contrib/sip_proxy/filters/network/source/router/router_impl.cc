#include "contrib/sip_proxy/filters/network/source/router/router_impl.h"

#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/logger.h"
#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/router/metadatamatchcriteria_impl.h"
#include "source/common/tracing/http_tracer_impl.h"

#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/route.pb.h"
#include "contrib/sip_proxy/filters/network/source/app_exception_impl.h"
#include "contrib/sip_proxy/filters/network/source/conn_manager.h"
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
    : RouteEntryImplBase(route), domain_(route.match().domain()), header_(route.match().header()),
      parameter_(route.match().parameter()) {}

RouteConstSharedPtr GeneralRouteEntryImpl::matches(MessageMetadata& metadata) const {
  absl::string_view header = "";
  // Default is route
  HeaderType type = HeaderType::Route;

  if (domain_.empty()) {
    return nullptr;
  }

  type = Envoy::Extensions::NetworkFilters::SipProxy::HeaderTypes::get().str2Header(header_);

  if (type == HeaderType::Other) {
    // Default is Route
    type = HeaderType::Route;
  }

  header = metadata.header(type).text();
  if (header.empty()) {
    if (type == HeaderType::Route) {
      if (metadata.header(HeaderType::TopLine).empty()) {
        ENVOY_LOG(error, "No route and r-uri");
        return nullptr;
      }
      header = metadata.header(HeaderType::TopLine).text();
      ENVOY_LOG(debug, "No route, r-uri {} is used ", header);
      type = HeaderType::TopLine;
    } else {
      ENVOY_LOG(debug, "header {} is empty", header_);
      return nullptr;
    }
  }

  if (domain_ == "*") {
    ENVOY_LOG(trace, "Route matched with domain: {}", domain_);
    return clusterEntry(metadata);
  }

  auto domain = metadata.getDomainFromHeaderParameter(type, parameter_);

  if (domain_ == domain) {
    ENVOY_LOG(trace, "Route matched with header: {}, parameter: {} and domain: {}", header_,
              parameter_, domain_);
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
      PANIC("not reached");
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
      transaction_info->deleteTransaction(callbacks_->transactionId());
    }
  }

  if (upstream_request_) {
    upstream_request_->delDecoderFilterCallbacks(*callbacks_);
  }
}

void Router::setDecoderFilterCallbacks(SipFilters::DecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
  transaction_infos_ = callbacks_->transactionInfos();
  settings_ = callbacks_->settings();
}

QueryStatus Router::handleCustomizedAffinity(const std::string& header, const std::string& type,
                                             const std::string& key,
                                             MessageMetadataSharedPtr metadata) {
  std::string host;
  QueryStatus ret = QueryStatus::Stop;

  auto handle_no_query = [&]() {
    auto header_type = HeaderType::Route;

    if (metadata->header(HeaderType::Route).empty()) {
      header_type = HeaderType::TopLine;
    }

    if (!header.empty()) {
      header_type = HeaderTypes::get().str2Header(header);
    }

    metadata->parseHeader(header_type);
    if (metadata->header(header_type).hasParam(type)) {
      host = std::string(metadata->header(header_type).param(type));
    }

    if (!host.empty()) {
      return QueryStatus::Continue;
    } else {
      return QueryStatus::Stop;
    }
  };

  if (type == "ep") {
    // For REGISTER, ep comes from Authorization header with opaque, and opaque is set when parse
    // Authorization
    if (metadata->methodType() == MethodType::Register && metadata->opaque().has_value()) {
      host = std::string(metadata->opaque().value());
    } else {
      // Handler other Requests except REGISTER
      ret = handle_no_query();
    }
  } else if (type == "text") {
    auto header_type = HeaderTypes::get().str2Header(header);

    ret = callbacks_->traHandler()->retrieveTrafficRoutingAssistant(
        header, std::string(metadata->header(header_type).text()), metadata->traContext(),
        *callbacks_, host);
  } else if (!(metadata->affinityIteration()->query())) {
    // query == false, eg. inst-ip=192.168.0.1, use value as host;
    // query == true, eg. lskpmc=S2F3, use value as key to query local cache or TRA.
    ret = handle_no_query();
  } else {
    ret = callbacks_->traHandler()->retrieveTrafficRoutingAssistant(
        type, key, metadata->traContext(), *callbacks_, host);
  }

  if (QueryStatus::Continue == ret) {
    metadata->setDestination(host);
    ENVOY_LOG(debug, "Set destination from local cache {} {} {} = {}", header, type, key,
              metadata->destination());
  }
  return ret;
}

FilterStatus Router::handleAffinity() {
  auto& metadata = metadata_;
  std::string host;

  // ONLY used for NOKIA P-Cookie-IP-Mapping
  if (metadata->pCookieIpMap().has_value()) {
    auto [key, val] = metadata->pCookieIpMap().value();
    ENVOY_LOG(trace, "update p-cookie-ip-map {}={}", key, val);
    callbacks_->traHandler()->updateTrafficRoutingAssistant("lskpmc", key, val, absl::nullopt);
  }

  const std::shared_ptr<const ProtocolOptionsConfig> options =
      cluster_->extensionProtocolOptionsTyped<ProtocolOptionsConfig>(
          SipFilters::SipFilterNames::get().SipProxy);

  if (options == nullptr || metadata->msgType() == MsgType::Response) {
    return FilterStatus::Continue;
  }

  // Do subscribe
  callbacks_->traHandler()->doSubscribe(options->customizedAffinity());

  if (metadata->affinity().empty()) {
    metadata->setStopLoadBalance(options->customizedAffinity().stop_load_balance());

    if (!options->customizedAffinity().entries().empty()) {
      for (const auto& aff : options->customizedAffinity().entries()) {
        // default header is Route, TOP-URI also used as Route
        HeaderType header = HeaderType::Route;
        if (!aff.header().empty()) {
          header = HeaderTypes::get().str2Header(aff.header());
          if (header == HeaderType::Other) {
            ENVOY_LOG(error, "header {} is not supported", aff.header());
            continue;
          }
        }
        auto type = aff.key_name();

        absl::string_view key = "";

        if (type == "text") {
          key = metadata->header(header).text();
        } else if (type == "ep") {
          key = "ep";
        } else {
          // If the header is Route, and the value is empty, then use the top-uri
          if ((header == HeaderType::Route) && (metadata->header(HeaderType::Route).empty())) {
            header = HeaderType::TopLine;
          }

          metadata->parseHeader(header);
          if (metadata->header(header).hasParam(type)) {
            key = metadata->header(header).param(type);
          }
        }

        if (!key.empty()) {
          metadata->affinity().emplace_back(aff.header(), type, std::string(key), aff.query(),
                                            aff.subscribe());
        }
      }
    } else if (metadata->methodType() != MethodType::Register && options->sessionAffinity()) {
      metadata->setStopLoadBalance(false);

      if (metadata->header(HeaderType::Route).empty()) {
        if (!metadata->header(HeaderType::TopLine).empty()) {
          metadata->parseHeader(HeaderType::TopLine);
          if (metadata->header(HeaderType::TopLine).hasParam("ep")) {
            metadata->affinity().emplace_back("Route", "ep", "ep", false, false);
          }
        }
      } else {
        metadata->parseHeader(HeaderType::Route);
        if (metadata->header(HeaderType::Route).hasParam("ep")) {
          metadata->affinity().emplace_back("Route", "ep", "ep", false, false);
        }
      }
    } else if (metadata->methodType() == MethodType::Register && options->registrationAffinity()) {
      metadata->setStopLoadBalance(false);

      // For REGISTER, opaque is set when parse Authorization, opaque works as same as ep
      if (metadata->opaque().has_value()) {
        metadata->affinity().emplace_back("Route", "ep", "ep", false, false);
      }
    }

    // NOTE: After constructed metadata->affinity(), must invoke
    // metadata->resetAffinityIteration() to set the iterator correctly. otherwise
    // affinityIteration can't point to the changed affinity correctly.
    metadata->resetAffinityIteration();
  }

  return FilterStatus::Continue;
}

FilterStatus Router::transportBegin(MessageMetadataSharedPtr metadata) {
  metadata_ = metadata;

  if (upstream_request_ != nullptr) {
    return FilterStatus::Continue;
  }

  metadata_ = metadata;
  route_ = callbacks_->route();
  if (!route_) {
    ENVOY_STREAM_LOG(debug, "no route matched", *callbacks_);
    stats_.route_missing_.inc();
    throw AppException(AppExceptionType::UnknownMethod, "envoy no match route found");
  }

  route_entry_ = route_->routeEntry();
  const std::string& cluster_name = route_entry_->clusterName();

  Upstream::ThreadLocalCluster* cluster = cluster_manager_.getThreadLocalCluster(cluster_name);
  if (!cluster) {
    ENVOY_STREAM_LOG(debug, "unknown cluster '{}'", *callbacks_, cluster_name);
    stats_.unknown_cluster_.inc();
    throw AppException(AppExceptionType::InternalError,
                       fmt::format("unknown cluster '{}'", cluster_name));
  }
  thread_local_cluster_ = cluster;

  cluster_ = cluster->info();
  ENVOY_STREAM_LOG(debug, "cluster '{}' matched", *callbacks_, cluster_name);

  if (cluster_->maintenanceMode()) {
    stats_.upstream_rq_maintenance_mode_.inc();
    throw AppException(AppExceptionType::InternalError,
                       fmt::format("maintenance mode for cluster '{}'", cluster_name));
  }

  handleAffinity();

  return FilterStatus::Continue;
}

FilterStatus Router::transportEnd() { return FilterStatus::Continue; }

FilterStatus
Router::messageHandlerWithLoadBalancer(std::shared_ptr<TransactionInfo> transaction_info,
                                       MessageMetadataSharedPtr metadata, std::string dest,
                                       bool& lb_ret) {
  auto conn_pool = thread_local_cluster_->tcpConnPool(Upstream::ResourcePriority::Default, this);
  if (!conn_pool) {
    stats_.no_healthy_upstream_.inc();
    if (dest.empty()) {
      throw AppException(AppExceptionType::InternalError,
                         fmt::format("envoy no healthy upstream endpoint during load balance"));
    } else {
      throw AppException(AppExceptionType::InternalError,
                         fmt::format("envoy no healthy upstream endpoint during affinity"));
    }
  }

  Upstream::HostDescriptionConstSharedPtr host = conn_pool->host();
  if (!host) {
    return FilterStatus::StopIteration;
  }

  // check the host ip is equal to dest. If false, then return StopIteration
  // if this function return StopIteration, then continue with next affinity
  if (!dest.empty() && dest != host->address()->ip()->addressAsString()) {
    ENVOY_LOG(info, "LB selected host {} is not equal to predefined dest {}",
              host->address()->ip()->addressAsString(), dest);
    return FilterStatus::StopIteration;
  }

  if (auto upstream_request =
          transaction_info->getUpstreamRequest(host->address()->ip()->addressAsString());
      upstream_request != nullptr) {
    // There is action connection, reuse it.
    upstream_request_ = upstream_request;
    upstream_request_->setDecoderFilterCallbacks(*callbacks_);
    upstream_request_->setMetadata(metadata);
    ENVOY_STREAM_LOG(debug, "reuse upstream request for {}", *callbacks_,
                     host->address()->ip()->addressAsString());

    transaction_info->insertTransaction(std::string(metadata->transactionId().value()), callbacks_,
                                        upstream_request_);
  } else {
    upstream_request_ = std::make_shared<UpstreamRequest>(
        std::make_shared<Upstream::TcpPoolData>(*conn_pool), transaction_info);
    upstream_request_->setDecoderFilterCallbacks(*callbacks_);
    upstream_request_->setMetadata(metadata);
    transaction_info->insertUpstreamRequest(host->address()->ip()->addressAsString(),
                                            upstream_request_);
    ENVOY_STREAM_LOG(debug, "create new upstream request {}", *callbacks_,
                     host->address()->ip()->addressAsString());

    transaction_info->insertTransaction(std::string(metadata->transactionId().value()), callbacks_,
                                        upstream_request_);
  }

  lb_ret = true;
  return upstream_request_->start();
}

FilterStatus Router::messageBegin(MessageMetadataSharedPtr metadata) {
  bool upstream_request_started = false;

  // ACK_4XX reuse
  if (upstream_request_ != nullptr &&
      upstream_request_->connectionState() == ConnectionState::Connected) {
    return FilterStatus::Continue;
  }

  auto& transaction_info = (*transaction_infos_)[cluster_->name()];

  if (!metadata->affinity().empty() &&
      metadata->affinityIteration() != metadata->affinity().end()) {
    std::string host;

    ENVOY_STREAM_LOG(debug, "handle affinity of header:{} type:{} key:{}", *callbacks_,
                     metadata->affinityIteration()->header(), metadata->affinityIteration()->type(),
                     metadata->affinityIteration()->key());

    if (!metadata->destination().empty()) {
      // TRA query get result, and set destintion.
      host = metadata->destination();
      ENVOY_STREAM_LOG(debug, "has already set destination {} from affinity", *callbacks_, host);
    } else {
      auto handle_ret = handleCustomizedAffinity(metadata->affinityIteration()->header(),
                                                 metadata->affinityIteration()->type(),
                                                 metadata->affinityIteration()->key(), metadata);

      if (QueryStatus::Continue == handle_ret) {
        // has already get the destination from affinity
        host = metadata->destination();
        ENVOY_STREAM_LOG(debug, "has already get destination {} from affinity", *callbacks_, host);
      } else if (QueryStatus::Pending == handle_ret) {
        ENVOY_STREAM_LOG(debug, "do remote query for {}", *callbacks_,
                         metadata->affinityIteration()->key());
        // Need to wait remote query response,
        // after response back, still back with current affinity
        metadata->setState(State::HandleAffinity);
        return FilterStatus::StopIteration;
      } else {
        ENVOY_STREAM_LOG(debug, "no existing destintion for {}", *callbacks_,
                         metadata->affinityIteration()->key());
        // Need to try next affinity
        metadata->nextAffinityIteration();
        metadata->setState(State::HandleAffinity);
        return FilterStatus::Continue;
      }
    }

    // Already get destintion for current affinity, try to get or create new connection for this
    // destination. If this destintion is invalid, try next affinity.
    if (auto upstream_request = transaction_info->getUpstreamRequest(std::string(host));
        upstream_request != nullptr) {
      // There is action connection, reuse it.
      ENVOY_STREAM_LOG(trace, "reuse upstream request from {}", *callbacks_, host);
      upstream_request_ = upstream_request;
      upstream_request_->setMetadata(metadata);
      upstream_request_->setDecoderFilterCallbacks(*callbacks_);

      transaction_info->insertTransaction(std::string(metadata->transactionId().value()),
                                          callbacks_, upstream_request_);
      ENVOY_STREAM_LOG(trace, "call upstream_request_->start()", *callbacks_);
      // Continue: continue to messageEnd, StopIteration: continue to next affinity
      if (FilterStatus::StopIteration == upstream_request_->start()) {
        // Defer to handle in upstream request onPoolReady or onPoolFailure
        ENVOY_LOG(trace, "sip: state {}", StateNameValues::name(metadata_->state()));
        return FilterStatus::StopIteration;
      }
      return FilterStatus::Continue;
    }

    ENVOY_STREAM_LOG(trace, "no destination preset select with load balancer host= {}", *callbacks_,
                     host);

    upstream_request_started = false;
    auto ret =
        messageHandlerWithLoadBalancer(transaction_info, metadata, host, upstream_request_started);
    if (upstream_request_started && ret == FilterStatus::StopIteration) {
      // Defer to handle in upstream request onPoolReady or onPoolFailure
      return FilterStatus::StopIteration;
    } else if (upstream_request_started && ret == FilterStatus::Continue) {
      return FilterStatus::Continue;
    } else {
      // continue to next affinity
      metadata->setState(State::HandleAffinity);
      ENVOY_LOG(trace, "sip: state {}", StateNameValues::name(metadata_->state()));
      metadata->nextAffinityIteration();
      return FilterStatus::Continue;
    }
  } else {
    metadata->resetDestination();
    if (!metadata->stopLoadBalance()) {
      ENVOY_STREAM_LOG(debug, "no destination from affinity, do load balance", *callbacks_);
      return messageHandlerWithLoadBalancer(transaction_info, metadata, "",
                                            upstream_request_started);
    } else {
      ENVOY_STREAM_LOG(debug, "no destination and stop load balance", *callbacks_);
      throw AppException(AppExceptionType::UnknownMethod, "envoy no endpoint found");
      return FilterStatus::StopIteration;
    }
  }
}

FilterStatus Router::messageEnd() {
  Buffer::OwnedImpl transport_buffer;

  // set EP/Opaque, used in upstream
  ENVOY_STREAM_LOG(debug, "set EP {}", *callbacks_, Utility::localAddress(context_));
  metadata_->setEP(Utility::localAddress(context_));

  std::shared_ptr<Encoder> encoder = std::make_shared<EncoderImpl>();
  encoder->encode(metadata_, transport_buffer);

  ENVOY_STREAM_LOG(debug, "send buffer : {} bytes\n{}", *callbacks_, transport_buffer.length(),
                   transport_buffer.toString());

  callbacks_->stats()
      .sipMethodCounter(metadata_->methodType(), SipMethodStatsSuffix::RequestProxied)
      .inc();
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

UpstreamRequest::UpstreamRequest(std::shared_ptr<Upstream::TcpPoolData> pool,
                                 std::shared_ptr<TransactionInfo> transaction_info)
    : conn_pool_(pool), transaction_info_(transaction_info) {}

UpstreamRequest::~UpstreamRequest() {
  if (conn_pool_handle_) {
    conn_pool_handle_->cancel(Tcp::ConnectionPool::CancelPolicy::Default);
  }
}

FilterStatus UpstreamRequest::start() {
  if (!callbacks_) {
    ENVOY_LOG(info, "There is no callback");
    return FilterStatus::StopIteration;
  }

  if (conn_state_ == ConnectionState::Connecting) {
    callbacks_->pushIntoPendingList("connection_pending", conn_pool_->host()->address()->asString(),
                                    *callbacks_, []() {});
    return FilterStatus::StopIteration;
  } else if (conn_state_ == ConnectionState::Connected) {
    return FilterStatus::Continue;
  }

  ENVOY_LOG(trace, "start connecting {}", conn_pool_->host()->address()->asString());
  conn_state_ = ConnectionState::Connecting;

  Tcp::ConnectionPool::Cancellable* handle = conn_pool_->newConnection(*this);
  if (handle) {
    // Pause while we wait for a connection.
    conn_pool_handle_ = handle;
    callbacks_->pushIntoPendingList("connection_pending", conn_pool_->host()->address()->asString(),
                                    *callbacks_, []() {});
    return FilterStatus::StopIteration;
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

void UpstreamRequest::onPoolFailure(ConnectionPool::PoolFailureReason reason, absl::string_view,
                                    Upstream::HostDescriptionConstSharedPtr host) {
  ENVOY_LOG(info, "on pool failure {} reason {}", host->address()->ip()->addressAsString(),
            static_cast<int>(reason));
  conn_state_ = ConnectionState::NotConnected;
  conn_pool_handle_ = nullptr;

  // Once onPoolFailure, this instance is invalid, can't be reused.
  transaction_info_->deleteUpstreamRequest(host->address()->ip()->addressAsString());

  if (callbacks_) {
    callbacks_->continueHandling(host->address()->asString(), true);
  }

  // Mimic an upstream reset.
  onUpstreamHostSelected(host);
}

void UpstreamRequest::onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn_data,
                                  Upstream::HostDescriptionConstSharedPtr host) {

  ENVOY_LOG(trace, "onPoolReady");
  bool continue_handling = conn_pool_handle_ != nullptr;

  conn_data_ = std::move(conn_data);

  onUpstreamHostSelected(host);
  conn_data_->addUpstreamCallbacks(*this);
  conn_pool_handle_ = nullptr;

  setConnectionState(ConnectionState::Connected);

  if (continue_handling) {
    if (callbacks_) {
      callbacks_->continueHandling(host->address()->asString(), false);
    }
  }
}

void UpstreamRequest::onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host) {
  upstream_host_ = host;
}

void UpstreamRequest::onResetStream(ConnectionPool::PoolFailureReason reason) {
  switch (reason) {
  case ConnectionPool::PoolFailureReason::Overflow:
    throw AppException(AppExceptionType::InternalError,
                       "sip upstream request: too many connections");
    break;
  case ConnectionPool::PoolFailureReason::LocalConnectionFailure:
    // Should only happen if we closed the connection, due to an error condition, in which case
    // we've already handled any possible downstream response.
    // callbacks_->resetDownstreamConnection();
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
    // callbacks_->resetDownstreamConnection();
    break;
  default:
    PANIC("not reached");
  }
}

SipFilters::DecoderFilterCallbacks* UpstreamRequest::getTransaction(std::string&& transaction_id) {
  if (transaction_info_->hasTransaction(transaction_id)) {
    return transaction_info_->getTransaction(std::move(transaction_id)).activeTrans();
  } else {
    return nullptr;
  }
}

// Tcp::ConnectionPool::UpstreamCallbacks
void UpstreamRequest::onUpstreamData(Buffer::Instance& data, bool end_stream) {
  UNREFERENCED_PARAMETER(end_stream);
  ENVOY_LOG(debug, "sip proxy received resp {} --> {} bytes {}",
            conn_data_->connection().connectionInfoProvider().remoteAddress()->asStringView(),
            conn_data_->connection().connectionInfoProvider().localAddress()->asStringView(),
            data.length());

  upstream_buffer_.move(data);
  auto response_decoder = std::make_unique<ResponseDecoder>(*this);
  response_decoder->onData(upstream_buffer_);
}

void UpstreamRequest::onEvent(Network::ConnectionEvent event) {
  ENVOY_LOG(info, "received upstream event {}", static_cast<int>(event));
  switch (event) {
  case Network::ConnectionEvent::RemoteClose:
    ENVOY_LOG(debug, "upstream remote close");
    break;
  case Network::ConnectionEvent::LocalClose:
    ENVOY_LOG(debug, "upstream local close");
    break;
  default:
    // Connected and ConnectedZeroRtt is consumed by the connection pool.
    return;
  }

  transaction_info_->deleteUpstreamRequest(upstream_host_->address()->ip()->addressAsString());
  releaseConnection(false);
}

void UpstreamRequest::setDecoderFilterCallbacks(SipFilters::DecoderFilterCallbacks& callbacks) {
  callbacks_ = &callbacks;
}

void UpstreamRequest::delDecoderFilterCallbacks(SipFilters::DecoderFilterCallbacks& callbacks) {
  if (callbacks_ == &callbacks) {
    callbacks_ = nullptr;
  }
}

bool ResponseDecoder::onData(Buffer::Instance& data) {
  decoder_->onData(data);
  return true;
}

FilterStatus ResponseDecoder::transportBegin(MessageMetadataSharedPtr metadata) {
  ENVOY_LOG(trace, "ResponseDecoder\n{}", metadata->rawMsg());
  if (metadata->transactionId().has_value()) {
    auto transaction_id = metadata->transactionId().value();

    auto active_trans = parent_.getTransaction(std::string(transaction_id));
    if (active_trans) {
      // ONLY used for NOKIA P-Cookie-IP-Mapping
      if (metadata->pCookieIpMap().has_value()) {
        auto [key, val] = metadata->pCookieIpMap().value();
        ENVOY_LOG(trace, "update p-cookie-ip-map {}={}", key, val);
        active_trans->traHandler()->updateTrafficRoutingAssistant("lskpmc", key, val,
                                                                  absl::nullopt);
      }

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

DecoderEventHandler& ResponseDecoder::newDecoderEventHandler(MessageMetadataSharedPtr metadata) {
  parent_.decoderFilterCallbacks()
      .stats()
      .sipMethodCounter(metadata->methodType(), SipMethodStatsSuffix::ResponseReceived)
      .inc();
  return *this;
}

std::shared_ptr<SipSettings> ResponseDecoder::settings() const { return parent_.settings(); }

} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
