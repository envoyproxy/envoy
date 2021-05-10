#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "envoy/extensions/filters/network/sip_proxy/v3/route.pb.h"
#include "envoy/router/router.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/tcp/conn_pool.h"
#include "envoy/upstream/load_balancer.h"

#include "common/common/logger.h"
#include "common/http/header_utility.h"
#include "common/upstream/load_balancer_impl.h"
#include "envoy/thread_local/thread_local.h"

#include "extensions/filters/network/sip_proxy/conn_manager.h"
#include "extensions/filters/network/sip_proxy/decoder_events.h"
#include "extensions/filters/network/sip_proxy/filters/filter.h"
#include "extensions/filters/network/sip_proxy/router/router.h"
#include "extensions/filters/network/sip_proxy/router/router_ratelimit_impl.h"

#include "absl/types/optional.h"
#include <iostream>
namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace Router {

class RouteEntryImplBase : public RouteEntry,
                           public Route,
                           public std::enable_shared_from_this<RouteEntryImplBase> {
public:
  RouteEntryImplBase(const envoy::extensions::filters::network::sip_proxy::v3::Route& route);

  // Router::RouteEntry
  const std::string& clusterName() const override;
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
    return metadata_match_criteria_.get();
  }
  // const RateLimitPolicy& rateLimitPolicy() const override { return rate_limit_policy_; }
  bool stripServiceName() const override { return strip_service_name_; };
  const Http::LowerCaseString& clusterHeader() const override { return cluster_header_; }

  // Router::Route
  const RouteEntry* routeEntry() const override;

  virtual RouteConstSharedPtr matches(MessageMetadata& metadata, uint64_t random_value) const PURE;

protected:
  RouteConstSharedPtr clusterEntry(uint64_t random_value, const MessageMetadata& metadata) const;
  bool headersMatch(const Http::HeaderMap& headers) const;

private:
  class DynamicRouteEntry : public RouteEntry, public Route {
  public:
    DynamicRouteEntry(const RouteEntryImplBase& parent, absl::string_view cluster_name)
        : parent_(parent), cluster_name_(std::string(cluster_name)) {}

    // Router::RouteEntry
    const std::string& clusterName() const override { return cluster_name_; }
    const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() const override {
      return parent_.metadataMatchCriteria();
    }
    // const RateLimitPolicy& rateLimitPolicy() const override { return parent_.rateLimitPolicy(); }
    bool stripServiceName() const override { return parent_.stripServiceName(); }
    const Http::LowerCaseString& clusterHeader() const override { return parent_.clusterHeader(); }

    // Router::Route
    const RouteEntry* routeEntry() const override { return this; }

  private:
    const RouteEntryImplBase& parent_;
    const std::string cluster_name_;
  };

  const std::string cluster_name_;
  const std::vector<Http::HeaderUtility::HeaderDataPtr> config_headers_;
  uint64_t total_cluster_weight_;
  Envoy::Router::MetadataMatchCriteriaConstPtr metadata_match_criteria_;
  // const RateLimitPolicyImpl rate_limit_policy_;
  const bool strip_service_name_;
  const Http::LowerCaseString cluster_header_;
};

using RouteEntryImplBaseConstSharedPtr = std::shared_ptr<const RouteEntryImplBase>;

// match domain from route header or request_uri, this is the more general way
class GeneralRouteEntryImpl : public RouteEntryImplBase {
public:
  GeneralRouteEntryImpl(const envoy::extensions::filters::network::sip_proxy::v3::Route& route);

  // RouteEntryImplBase
  RouteConstSharedPtr matches(MessageMetadata& metadata, uint64_t random_value) const override;

private:
  const std::string domain_;
  const bool invert_;
};

class RouteMatcher {
public:
  RouteMatcher(const envoy::extensions::filters::network::sip_proxy::v3::RouteConfiguration&);

  RouteConstSharedPtr route(MessageMetadata& metadata, uint64_t random_value) const;

private:
  std::vector<RouteEntryImplBaseConstSharedPtr> routes_;
};

#define ALL_SIP_ROUTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                            \
  COUNTER(route_missing)                                                                           \
  COUNTER(unknown_cluster)                                                                         \
  COUNTER(upstream_rq_maintenance_mode)                                                            \
  COUNTER(no_healthy_upstream)

struct RouterStats {
  ALL_SIP_ROUTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)
};

class UpstreamRequest;
class TransactionInfoItem : public Logger::Loggable<Logger::Id::filter> {
public:
  TransactionInfoItem(SipFilters::DecoderFilterCallbacks* active_trans,
                      std::shared_ptr<UpstreamRequest> upstream_request)
      : active_trans_(active_trans), upstream_request_(upstream_request),
        timestamp_(std::chrono::system_clock::now()) {}

  ~TransactionInfoItem() {}

  void resetTrans() { active_trans_->onReset(); }

  void appendMessageList(std::shared_ptr<MessageMetadata> message) { messages_.push_back(message); }

  SipFilters::DecoderFilterCallbacks* activeTrans() const { return active_trans_; }
  std::shared_ptr<UpstreamRequest> upstreamRequest() const { return upstream_request_; }

  std::chrono::system_clock::time_point timestamp() const { return timestamp_; }
  void toDelete() { deleted_ = true; }
  bool deleted() { return deleted_; }

private:
  std::list<std::shared_ptr<MessageMetadata>> messages_;
  SipFilters::DecoderFilterCallbacks* active_trans_;
  std::shared_ptr<UpstreamRequest> upstream_request_;
  std::chrono::system_clock::time_point timestamp_;
  bool deleted_{false};
};

struct ThreadLocalTransactionInfo : public ThreadLocal::ThreadLocalObject,
                                    public Logger::Loggable<Logger::Id::filter> {
  ThreadLocalTransactionInfo(std::shared_ptr<TransactionInfo> parent, Event::Dispatcher& dispatcher,
                             std::chrono::milliseconds transaction_timeout)
      : parent_(parent), dispatcher_(dispatcher),
        audit_timer_(dispatcher.createTimer([this]() -> void { auditTimerAction(); })),
        transaction_timeout_(transaction_timeout) {
    audit_timer_->enableTimer(std::chrono::seconds(2));
  }
  std::unordered_map<std::string, std::shared_ptr<TransactionInfoItem>> transaction_info_map_{};
  std::unordered_map<std::string, std::shared_ptr<UpstreamRequest>> upstream_request_map_{};

  std::shared_ptr<TransactionInfo> parent_;
  Event::Dispatcher& dispatcher_;
  Event::TimerPtr audit_timer_;
  std::chrono::milliseconds transaction_timeout_;

  void auditTimerAction() {
    const auto p1 = std::chrono::system_clock::now();
    for (auto it = transaction_info_map_.cbegin(); it != transaction_info_map_.cend();) {
      if (it->second->deleted()) {
        it = transaction_info_map_.erase(it);
      } else {
        auto diff =
            std::chrono::duration_cast<std::chrono::milliseconds>(p1 - it->second->timestamp());
        if (diff.count() >= transaction_timeout_.count()) {
          it->second->resetTrans();
          // transaction_info_map_.erase(it++);
        }

        if (it->second->deleted()) {
          it = transaction_info_map_.erase(it);
        } else {
          ++it;
        }
      }
    }
    audit_timer_->enableTimer(std::chrono::seconds(2));
  }
};

class TransactionInfo : public std::enable_shared_from_this<TransactionInfo>,
                        Logger::Loggable<Logger::Id::filter> {
public:
  TransactionInfo(const std::string& cluster_name, ThreadLocal::SlotAllocator& tls,
                  std::chrono::milliseconds transaction_timeout)
      : cluster_name_(cluster_name), tls_(tls.allocateSlot()),
        transaction_timeout_(transaction_timeout) {}

  void init() {
    // Note: `this` and `cluster_name` have a a lifetime of the filter.
    // That may be shorter than the tls callback if the listener is torn down shortly after it is
    // created. We use a weak pointer to make sure this object outlives the tls callbacks.
    std::weak_ptr<TransactionInfo> this_weak_ptr = this->shared_from_this();
    tls_->set(
        [this_weak_ptr](Event::Dispatcher& dispatcher) -> ThreadLocal::ThreadLocalObjectSharedPtr {
          if (auto this_shared_ptr = this_weak_ptr.lock()) {
            return std::make_shared<ThreadLocalTransactionInfo>(
                this_shared_ptr, dispatcher, this_shared_ptr->transaction_timeout_);
          }
          return nullptr;
        });

    (void)cluster_name_;
  }
  ~TransactionInfo() = default;

  void insertTransaction(std::string&& transaction_id,
                         SipFilters::DecoderFilterCallbacks* active_trans,
                         std::shared_ptr<UpstreamRequest> upstream_request) {
    //    tls_->getTyped<ThreadLocalTransactionInfo>().transaction_info_map_.emplace(std::piecewise_construct,
    //        std::forward_as_tuple(transaction_id), std::forward_as_tuple(active_trans,
    //        upstream_request) ); std::make_pair(transaction_id, TransactionInfoItem(active_trans,
    //        upstream_request)));
    // ENVOY_LOG(error, "insert transaction {}", transaction_id);
    tls_->getTyped<ThreadLocalTransactionInfo>().transaction_info_map_.emplace(std::make_pair(
        transaction_id, std::make_shared<TransactionInfoItem>(active_trans, upstream_request)));
  }

  void deleteTransaction(std::string&& transaction_id) {
    tls_->getTyped<ThreadLocalTransactionInfo>()
        .transaction_info_map_.at(transaction_id)
        ->toDelete();
  }

  TransactionInfoItem& getTransaction(std::string&& transaction_id) {
    return *(tls_->getTyped<ThreadLocalTransactionInfo>().transaction_info_map_.at(transaction_id));
  }

  void insertUpstreamRequest(const std::string& host,
                             std::shared_ptr<UpstreamRequest> upstream_request) {
    // tls_->getTyped<ThreadLocalTransactionInfo>().upstream_request_map_[host] = upstream_request;
    tls_->getTyped<ThreadLocalTransactionInfo>().upstream_request_map_.emplace(
        std::make_pair(host, upstream_request));
  }

  std::shared_ptr<UpstreamRequest> getUpstreamRequest(const std::string& host) {
    try {
      return tls_->getTyped<ThreadLocalTransactionInfo>().upstream_request_map_.at(host);
    } catch (std::out_of_range) {
      return nullptr;
    }
  }

  void deleteUpstreamRequest(const std::string& host) {
    tls_->getTyped<ThreadLocalTransactionInfo>().upstream_request_map_.erase(host);
  }

private:
  const std::string cluster_name_;
  ThreadLocal::SlotPtr tls_;
  std::chrono::milliseconds transaction_timeout_;
};

class Router : public Upstream::LoadBalancerContextBase,
               public virtual DecoderEventHandler,
               public SipFilters::DecoderFilter,
               Logger::Loggable<Logger::Id::filter> {
public:
  Router(Upstream::ClusterManager& cluster_manager, const std::string& stat_prefix,
         Stats::Scope& scope)
      : cluster_manager_(cluster_manager), stats_(generateStats(stat_prefix, scope)) {}

  // SipFilters::DecoderFilter
  void onDestroy() override;
  void setDecoderFilterCallbacks(SipFilters::DecoderFilterCallbacks& callbacks) override;

  // DecoderEventHandler
  FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus transportEnd() override;
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus messageEnd() override;

  // Upstream::LoadBalancerContext
  const Network::Connection* downstreamConnection() const override;
  const Envoy::Router::MetadataMatchCriteria* metadataMatchCriteria() override {
    if (route_entry_) {
      return route_entry_->metadataMatchCriteria();
    }
    return nullptr;
  }

private:
  void cleanup();
  RouterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return RouterStats{ALL_SIP_ROUTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                            POOL_GAUGE_PREFIX(scope, prefix),
                                            POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }

  Upstream::ClusterManager& cluster_manager_;
  RouterStats stats_;

  RouteConstSharedPtr route_{};
  const RouteEntry* route_entry_{};
  MessageMetadataSharedPtr metadata_{};

  std::shared_ptr<UpstreamRequest> upstream_request_;
  SipFilters::DecoderFilterCallbacks* callbacks_{};
  Upstream::ClusterInfoConstSharedPtr cluster_;
  std::shared_ptr<TransactionInfos> transaction_infos_{};
  std::shared_ptr<SipSettings> settings_;
};

class ThreadLocalActiveConn;
class ResponseDecoder : public DecoderCallbacks, public DecoderEventHandler {
public:
  ResponseDecoder(UpstreamRequest& parent)
      : parent_(parent), decoder_(std::make_unique<Decoder>(*this)) {}
  bool onData(Buffer::Instance& data);

  // DecoderEventHandler
  FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override {
    UNREFERENCED_PARAMETER(metadata);
    return FilterStatus::Continue;
  }
  FilterStatus messageEnd() override { return FilterStatus::Continue; };
  FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
  FilterStatus transportEnd() override { return FilterStatus::Continue; }

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler(MessageMetadataSharedPtr metadata) override {
    UNREFERENCED_PARAMETER(metadata);
    return *this;
  }

private:
  UpstreamRequest& parent_;
  DecoderPtr decoder_;
  //  MessageMetadataSharedPtr metadata_;
  //  absl::optional<bool> success_;
  //  bool complete_ : 1;
  //  bool first_reply_field_ : 1;
};
using ResponseDecoderPtr = std::unique_ptr<ResponseDecoder>;

class UpstreamRequest : public Tcp::ConnectionPool::Callbacks,
                        public Tcp::ConnectionPool::UpstreamCallbacks,
                        public std::enable_shared_from_this<UpstreamRequest>,
                        public Logger::Loggable<Logger::Id::filter> {
public:
  UpstreamRequest(Tcp::ConnectionPool::Instance& pool,
                  std::shared_ptr<TransactionInfo> transaction_info);
  ~UpstreamRequest() override;
  FilterStatus start();
  void resetStream();
  void releaseConnection(bool close);

  SipFilters::DecoderFilterCallbacks* getTransaction(std::string&& transaction_id);
  std::shared_ptr<Tcp::ConnectionPool::ConnectionData> connData() { return conn_data_; }

  // Tcp::ConnectionPool::Callbacks
  void onPoolFailure(ConnectionPool::PoolFailureReason reason,
                     Upstream::HostDescriptionConstSharedPtr host) override;
  void onPoolReady(Tcp::ConnectionPool::ConnectionDataPtr&& conn,
                   Upstream::HostDescriptionConstSharedPtr host) override;

  void onRequestStart(bool continue_decoding);
  void onRequestComplete();
  void onResponseComplete();
  void onUpstreamHostSelected(Upstream::HostDescriptionConstSharedPtr host);
  void onResetStream(ConnectionPool::PoolFailureReason reason);

  // Tcp::ConnectionPool::UpstreamCallbacks
  void onUpstreamData(Buffer::Instance& data, bool end_stream) override;
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  void setDecoderFilterCallbacks(SipFilters::DecoderFilterCallbacks& callbacks);

  void addIntoPendingRequest(MessageMetadataSharedPtr metadata) {
    if (pending_request_.size() < 1000000) {
      pending_request_.push_back(metadata);
    } else {
      ENVOY_LOG(warn, "pending request is full, drop this request. size {} request {}",
                pending_request_.size(), metadata->rawMsg());
    }
  }

  ConnectionState connectionState() { return conn_state_; }
  void write(Buffer::Instance& data, bool end_stream) {
    return conn_data_->connection().write(data, end_stream);
  }

  absl::string_view localAddress() {
    return conn_data_->connection().addressProvider().localAddress()->ip()->addressAsString();
  }

private:
  Tcp::ConnectionPool::Instance& conn_pool_;

  Tcp::ConnectionPool::Cancellable* conn_pool_handle_{};
  std::shared_ptr<Tcp::ConnectionPool::ConnectionData> conn_data_;
  Upstream::HostDescriptionConstSharedPtr upstream_host_;
  ConnectionState conn_state_{ConnectionState::NotConnected};

  std::shared_ptr<TransactionInfo> transaction_info_;
  SipFilters::DecoderFilterCallbacks* callbacks_{};
  std::list<MessageMetadataSharedPtr> pending_request_;
  Buffer::OwnedImpl upstream_buffer_;

  bool request_complete_ : 1;
//  bool response_started_ : 1;
  bool response_complete_ : 1;
};

} // namespace Router
} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
