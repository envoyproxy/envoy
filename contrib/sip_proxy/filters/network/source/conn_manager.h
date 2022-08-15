#pragma once

#include <cstddef>
#include <memory>

#include "envoy/common/pure.h"
#include "envoy/common/random_generator.h"
#include "envoy/event/deferred_deletable.h"
#include "envoy/network/connection.h"
#include "envoy/network/filter.h"
#include "envoy/stats/timespan.h"
#include "envoy/upstream/upstream.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/linked_object.h"
#include "source/common/common/logger.h"
#include "source/common/common/random_generator.h"
#include "source/common/config/utility.h"
#include "source/common/stats/timespan_impl.h"
#include "source/common/stream_info/stream_info_impl.h"
#include "source/common/tracing/http_tracer_impl.h"

#include "absl/types/any.h"
#include "contrib/sip_proxy/filters/network/source/decoder.h"
#include "contrib/sip_proxy/filters/network/source/filters/filter.h"
#include "contrib/sip_proxy/filters/network/source/stats.h"
#include "contrib/sip_proxy/filters/network/source/tra/tra_impl.h"
#include "contrib/sip_proxy/filters/network/source/utility.h"
#include "metadata.h"
#include "router/router.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

/**
 * Config is a configuration interface for ConnectionManager.
 */
class SipSettings;
class Config {
public:
  virtual ~Config() = default;

  virtual SipFilters::FilterChainFactory& filterFactory() PURE;
  virtual SipFilterStats& stats() PURE;
  virtual Router::Config& routerConfig() PURE;
  virtual std::shared_ptr<SipSettings> settings() PURE;
};

class ConnectionManager;

// Thread local wrapper aorund our map conn-id -> DownstreamConnection
struct ThreadLocalDownstreamConnectionInfo : public ThreadLocal::ThreadLocalObject,
                                             public Logger::Loggable<Logger::Id::filter> {
  ThreadLocalDownstreamConnectionInfo(std::shared_ptr<DownstreamConnectionInfos> parent)
      : parent_(parent) {}
  absl::flat_hash_map<std::string, std::shared_ptr<SipFilters::DecoderFilterCallbacks>>
      downstream_connection_info_map_{};

  std::shared_ptr<DownstreamConnectionInfos> parent_;
};

/**
 * Extends Upstream::ProtocolOptionsConfig with Sip-specific cluster options.
 */
class ProtocolOptionsConfig : public Upstream::ProtocolOptionsConfig {
public:
  ~ProtocolOptionsConfig() override = default;

  virtual bool sessionAffinity() const PURE;
  virtual bool registrationAffinity() const PURE;
  virtual const envoy::extensions::filters::network::sip_proxy::v3alpha::CustomizedAffinity&
  customizedAffinity() const PURE;
};

class TrafficRoutingAssistantHandler : public TrafficRoutingAssistant::RequestCallbacks,
                                       public Logger::Loggable<Logger::Id::filter> {
public:
  TrafficRoutingAssistantHandler(
      ConnectionManager& parent, Event::Dispatcher& dispatcher,
      const envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceConfig& config,
      Server::Configuration::FactoryContext& context, StreamInfo::StreamInfoImpl& stream_info);

  virtual void updateTrafficRoutingAssistant(const std::string& type, const std::string& key,
                                             const std::string& val,
                                             const absl::optional<TraContextMap> context);
  virtual QueryStatus retrieveTrafficRoutingAssistant(
      const std::string& type, const std::string& key, const absl::optional<TraContextMap> context,
      SipFilters::DecoderFilterCallbacks& activetrans, std::string& host);
  virtual void deleteTrafficRoutingAssistant(const std::string& type, const std::string& key,
                                             const absl::optional<TraContextMap> context);
  virtual void subscribeTrafficRoutingAssistant(const std::string& type);
  void complete(const TrafficRoutingAssistant::ResponseType& type, const std::string& message_type,
                const absl::any& resp) override;
  void
  doSubscribe(const envoy::extensions::filters::network::sip_proxy::v3alpha::CustomizedAffinity&
                  customized_affinity);

private:
  virtual TrafficRoutingAssistant::ClientPtr& traClient() { return tra_client_; }

  ConnectionManager& parent_;
  CacheManager<std::string, std::string, std::string> cache_manager_;
  TrafficRoutingAssistant::ClientPtr tra_client_;
  StreamInfo::StreamInfoImpl stream_info_;
  std::map<std::string, bool> is_subscribe_map_;
};

/**
 * ConnectionManager is a Network::Filter that will perform Sip request handling on a connection.
 */
class ConnectionManager : public Network::ReadFilter,
                          public Network::ConnectionCallbacks,
                          public DecoderCallbacks,
                          public PendingListHandler,
                          Logger::Loggable<Logger::Id::connection> {
public:
  ConnectionManager(
      Config& config, Random::RandomGenerator& random_generator, TimeSource& time_system,
      Server::Configuration::FactoryContext& context,
      std::shared_ptr<Router::TransactionInfos> transaction_infos,
      std::shared_ptr<SipProxy::DownstreamConnectionInfos> downstream_connections_info,
      std::shared_ptr<SipProxy::UpstreamTransactionInfos> upstream_transaction_info);
  ~ConnectionManager() override;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override;

  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks&) override;

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // DecoderCallbacks
  DecoderEventHandler& newDecoderEventHandler(MessageMetadataSharedPtr metadata) override;

  std::shared_ptr<SipSettings> settings() const override { return config_.settings(); }

  void continueHandling(const std::string& key, bool try_next_affinity = false);
  void continueHandling(MessageMetadataSharedPtr metadata,
                        DecoderEventHandler& decoder_event_handler);
  std::shared_ptr<TrafficRoutingAssistantHandler> traHandler() { return this->tra_handler_; }

  // PendingListHandler
  void pushIntoPendingList(const std::string& type, const std::string& key,
                           SipFilters::DecoderFilterCallbacks& activetrans,
                           std::function<void(void)> func) override {
    return pending_list_.pushIntoPendingList(type, key, activetrans, func);
  }
  void onResponseHandleForPendingList(
      const std::string& type, const std::string& key,
      std::function<void(MessageMetadataSharedPtr metadata, DecoderEventHandler&)> func) override {
    return pending_list_.onResponseHandleForPendingList(type, key, func);
  }
  void eraseActiveTransFromPendingList(std::string& transaction_id) override {
    return pending_list_.eraseActiveTransFromPendingList(transaction_id);
  }

private:
  friend class SipConnectionManagerTest;
  friend class DownstreamConnectionInfos; // Needs to access DownstreamConnection private
                                          // inner class
  friend class UpstreamTransactionInfos;  // Needs to access UpstreamActiveTrans private inner class
  friend struct ThreadLocalUpstreamTransactionInfo; // Needs to access UpstreamActiveTrans private
                                                     // inner class

  struct ActiveTrans;

  struct UpstreamMessageDecoder : public DecoderCallbacks, public DecoderEventHandler {
    UpstreamMessageDecoder(ActiveTrans& parent) : parent_(parent) {}

    bool onData(MessageMetadataSharedPtr metadata);

    // DecoderEventHandler
    FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus messageEnd() override;
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override {
      UNREFERENCED_PARAMETER(metadata);
      return FilterStatus::Continue;
    }
    FilterStatus transportEnd() override;

    // DecoderCallbacks
    DecoderEventHandler& newDecoderEventHandler(MessageMetadataSharedPtr metadata) override {
      UNREFERENCED_PARAMETER(metadata);
      return *this;
    }

    std::shared_ptr<SipSettings> settings() const override { return parent_.parent_.settings(); }

    std::shared_ptr<TrafficRoutingAssistantHandler> traHandler() {
      return parent_.parent_.tra_handler_;
    }

    ActiveTrans& parent_;
    MessageMetadataSharedPtr metadata_;
  };
  using UpstreamMessageDecoderPtr = std::unique_ptr<UpstreamMessageDecoder>;

  // Wraps a DecoderFilter and acts as the DecoderFilterCallbacks for the filter, enabling filter
  // chain continuation.
  struct ActiveTransDecoderFilter : public SipFilters::DecoderFilterCallbacks,
                                    LinkedObject<ActiveTransDecoderFilter> {
    ActiveTransDecoderFilter(ActiveTrans& parent, SipFilters::DecoderFilterSharedPtr filter)
        : parent_(parent), handle_(filter) {}

    // SipFilters::DecoderFilterCallbacks
    uint64_t streamId() const override { return parent_.streamId(); }
    std::string transactionId() const override { return parent_.transactionId(); }
    const Network::Connection* connection() const override { return parent_.connection(); }
    absl::optional<OriginIngress> originIngress() override { return parent_.originIngress(); }
    Router::RouteConstSharedPtr route() override { return parent_.route(); }
    SipFilterStats& stats() override { return parent_.stats(); }
    void sendLocalReply(const DirectResponse& response, bool end_stream) override {
      parent_.sendLocalReply(response, end_stream);
    }
    void startUpstreamResponse() override { parent_.startUpstreamResponse(); }
    SipFilters::ResponseStatus upstreamData(MessageMetadataSharedPtr metadata,
                                      Router::RouteConstSharedPtr return_route,
                                            const std::string& return_destination) override {
      return parent_.upstreamData(metadata, return_route, return_destination);
    }
    void resetDownstreamConnection() override { parent_.resetDownstreamConnection(); }
    StreamInfo::StreamInfo& streamInfo() override { return parent_.streamInfo(); }
    std::shared_ptr<Router::TransactionInfos> transactionInfos() override {
      return parent_.transactionInfos();
    }
    std::shared_ptr<DownstreamConnectionInfos> downstreamConnectionInfos() override {
      return parent_.downstreamConnectionInfos();
    }
    std::shared_ptr<SipProxy::UpstreamTransactionInfos> upstreamTransactionInfo() override {
      return parent_.upstreamTransactionInfo();
    }
    std::shared_ptr<SipSettings> settings() const override { return parent_.settings(); }
    std::shared_ptr<TrafficRoutingAssistantHandler> traHandler() override {
      return parent_.traHandler();
    }
    void onReset() override { return parent_.onReset(); }

    void continueHandling(const std::string& key, bool try_next_affinity) override {
      return parent_.continueHandling(key, try_next_affinity);
    }
    MessageMetadataSharedPtr metadata() override { return parent_.metadata(); }

    // PendingListHandler
    void pushIntoPendingList(const std::string& type, const std::string& key,
                             SipFilters::DecoderFilterCallbacks& activetrans,
                             std::function<void(void)> func) override {
      UNREFERENCED_PARAMETER(type);
      UNREFERENCED_PARAMETER(key);
      UNREFERENCED_PARAMETER(activetrans);
      UNREFERENCED_PARAMETER(func);
    }
    void onResponseHandleForPendingList(
        const std::string& type, const std::string& key,
        std::function<void(MessageMetadataSharedPtr, DecoderEventHandler&)> func) override {
      UNREFERENCED_PARAMETER(type);
      UNREFERENCED_PARAMETER(key);
      UNREFERENCED_PARAMETER(func);
    }
    void eraseActiveTransFromPendingList(std::string& transaction_id) override {
      UNREFERENCED_PARAMETER(transaction_id);
    }

    ActiveTrans& parent_;
    SipFilters::DecoderFilterSharedPtr handle_;
  };
  using ActiveTransDecoderFilterPtr = std::unique_ptr<ActiveTransDecoderFilter>;

  // ActiveTrans tracks request/response pairs.
  struct ActiveTrans : LinkedObject<ActiveTrans>,
                       public Event::DeferredDeletable,
                       public DecoderEventHandler,
                       public SipFilters::DecoderFilterCallbacks,
                       public SipFilters::FilterChainFactoryCallbacks {
    ActiveTrans(ConnectionManager& parent, MessageMetadataSharedPtr metadata)
        : parent_(parent), stream_id_(parent_.random_generator_.random()),
          transaction_id_(metadata->transactionId().value()),
          stream_info_(parent_.time_source_,
                       parent_.read_callbacks_->connection().connectionInfoProviderSharedPtr()),
          metadata_(metadata) {}
    ~ActiveTrans() override {
      for (auto& filter : decoder_filters_) {
        filter->handle_->onDestroy();
      }
    }

    // DecoderEventHandler
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus transportEnd() override;
    FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus messageEnd() override;

    // SipFilters::DecoderFilterCallbacks
    uint64_t streamId() const override { return stream_id_; }
    std::string transactionId() const override { return transaction_id_; }
    absl::optional<OriginIngress> originIngress() override { return parent_.local_origin_ingress_; }
    const Network::Connection* connection() const override;
    SipFilterStats& stats() override { return parent_.stats_; }
    void resetDownstreamConnection() override;
    StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }

    std::shared_ptr<Router::TransactionInfos> transactionInfos() override {
      return parent_.transaction_infos_;
    }
    std::shared_ptr<SipProxy::DownstreamConnectionInfos> downstreamConnectionInfos() override {
      return parent_.downstream_connection_infos_;
    }
    std::shared_ptr<SipProxy::UpstreamTransactionInfos> upstreamTransactionInfo() override {
      return parent_.upstream_transaction_infos_;
    }
    std::shared_ptr<SipSettings> settings() const override { return parent_.config_.settings(); }
    std::shared_ptr<TrafficRoutingAssistantHandler> traHandler() override {
      return parent_.tra_handler_;
    }
    void continueHandling(const std::string& key, bool try_next_affinity) override {
      return parent_.continueHandling(key, try_next_affinity);
    }

    // Sip::FilterChainFactoryCallbacks
    void addDecoderFilter(SipFilters::DecoderFilterSharedPtr filter) override {
      ActiveTransDecoderFilterPtr wrapper =
          std::make_unique<ActiveTransDecoderFilter>(*this, filter);
      filter->setDecoderFilterCallbacks(wrapper->parent_);
      LinkedList::moveIntoListBack(std::move(wrapper), decoder_filters_);
    }

    FilterStatus applyDecoderFilters(ActiveTransDecoderFilter* filter);
    void finalizeRequest();

    void createFilterChain();
    virtual void onError(const std::string& what) PURE;
    MessageMetadataSharedPtr metadata() override { return metadata_; }
    bool localResponseSent() { return local_response_sent_; }
    void setLocalResponseSent(bool local_response_sent) {
      local_response_sent_ = local_response_sent;
    }

    ConnectionManager& parent_;
    Stats::TimespanPtr request_timer_;
    uint64_t stream_id_;
    std::string transaction_id_;
    StreamInfo::StreamInfoImpl stream_info_;
    MessageMetadataSharedPtr metadata_;
    std::list<ActiveTransDecoderFilterPtr> decoder_filters_;
    UpstreamMessageDecoderPtr response_decoder_;
    absl::optional<Router::RouteConstSharedPtr> cached_route_;

    std::function<FilterStatus(DecoderEventHandler*)> filter_action_;

    absl::any filter_context_;
    bool local_response_sent_{false};

    /* Used by Router */
    std::shared_ptr<Router::TransactionInfos> transaction_infos_;
  };

  struct DownstreamActiveTrans : public ActiveTrans {
    DownstreamActiveTrans(ConnectionManager& parent, MessageMetadataSharedPtr metadata)
        : ActiveTrans(parent, metadata) {
      parent.stats_.request_active_.inc();
      request_timer_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(parent_.stats_.request_time_ms_, parent_.time_source_);
    }

    ~DownstreamActiveTrans() override {
      ENVOY_LOG(debug, "DownstreamActiveTrans Dtor");
      request_timer_->complete();
      parent_.stats_.request_active_.dec();

      parent_.eraseActiveTransFromPendingList(transaction_id_);
    }

    // DecoderEventHandler
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;

    // PendingListHandler
    void pushIntoPendingList(const std::string& type, const std::string& key,
                             SipFilters::DecoderFilterCallbacks& activetrans,
                             std::function<void(void)> func) override {
      return parent_.pushIntoPendingList(type, key, activetrans, func);
    }
    void onResponseHandleForPendingList(
        const std::string& type, const std::string& key,
        std::function<void(MessageMetadataSharedPtr metadata, DecoderEventHandler&)> func)
        override {
      return parent_.onResponseHandleForPendingList(type, key, func);
    }
    void eraseActiveTransFromPendingList(std::string& transaction_id) override {
      return parent_.eraseActiveTransFromPendingList(transaction_id);
    }
    
    // SipFilters::DecoderFilterCallbacks
    Router::RouteConstSharedPtr route() override;
    void sendLocalReply(const DirectResponse& response, bool end_stream) override;
    SipFilters::ResponseStatus upstreamData(MessageMetadataSharedPtr metadata,
                                      Router::RouteConstSharedPtr return_route,
                                      const std::string& return_destination) override;
    void startUpstreamResponse() override;

    void onError(const std::string& what) override;

    void onReset() override;
  };

  struct UpstreamActiveTrans : public ActiveTrans {
    UpstreamActiveTrans(ConnectionManager& parent, MessageMetadataSharedPtr metadata)
        : ActiveTrans(parent, metadata) {
      parent.stats_.upstream_request_active_.inc();
      request_timer_ = std::make_unique<Stats::HistogramCompletableTimespanImpl>(parent_.stats_.upstream_request_time_ms_, parent_.time_source_);
    }

    ~UpstreamActiveTrans() override { 
      ENVOY_LOG(debug, "UpstreamActiveTrans Dtor"); 
      request_timer_->complete();
      parent_.stats_.upstream_request_active_.dec();
    }

    // DecoderEventHandler
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;

    // PendingListHandler
    void pushIntoPendingList(const std::string& type, const std::string& key,
                             SipFilters::DecoderFilterCallbacks& activetrans,
                             std::function<void(void)> func) override {
      UNREFERENCED_PARAMETER(type);
      UNREFERENCED_PARAMETER(key);
      UNREFERENCED_PARAMETER(activetrans);
      UNREFERENCED_PARAMETER(func);
    }
    void onResponseHandleForPendingList(
        const std::string& type, const std::string& key,
        std::function<void(MessageMetadataSharedPtr metadata, DecoderEventHandler&)> func)
        override {
      UNREFERENCED_PARAMETER(type);
      UNREFERENCED_PARAMETER(key);
      UNREFERENCED_PARAMETER(func);
    }
    void eraseActiveTransFromPendingList(std::string& transaction_id) override {
      UNREFERENCED_PARAMETER(transaction_id);
    }
    
    // SipFilters::DecoderFilterCallbacks
    Router::RouteConstSharedPtr route() override { return return_route_; };
    void sendLocalReply(const DirectResponse& response, bool end_stream) override;
    SipFilters::ResponseStatus upstreamData(MessageMetadataSharedPtr metadata,
                                      Router::RouteConstSharedPtr return_route,
                                      const std::string& return_destination) override;
    void startUpstreamResponse() override;

    void resetDownstreamConnection() override;
    StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }

    std::shared_ptr<SipSettings> settings() const override { return parent_.config_.settings(); }
    void onReset() override;
    void continueHandling(const std::string& key, bool try_next_affinity) override {
      UNREFERENCED_PARAMETER(key);
      UNREFERENCED_PARAMETER(try_next_affinity);
    }

    void onError(const std::string& what) override;

    Router::RouteConstSharedPtr return_route_;
    std::string return_destination_;
    MessageMetadataSharedPtr metadata_;
  };

  // Wrapper around a connection to enable routing of requests from upstream to downstream
  class DownstreamConnection : public SipFilters::DecoderFilterCallbacks,
                                       Logger::Loggable<Logger::Id::filter> {
  public:
    DownstreamConnection(ConnectionManager& parent);
    ~DownstreamConnection() override = default;

    // // // SipFilters::DecoderFilterCallbacks
    const Network::Connection* connection() const override;
    SipFilterStats& stats() override;
    std::shared_ptr<SipSettings> settings() const override;

    uint64_t streamId() const override { return 0; }
    std::string transactionId() const override { return ""; }
    absl::optional<OriginIngress> originIngress() override { return OriginIngress{"", ""}; }

    Router::RouteConstSharedPtr route() override { return nullptr; }

    void sendLocalReply(const DirectResponse& response, bool end_stream) override {
      UNREFERENCED_PARAMETER(response);
      UNREFERENCED_PARAMETER(end_stream);
    }

    void startUpstreamResponse() override;

    SipFilters::ResponseStatus upstreamData(MessageMetadataSharedPtr metadata,
                                      Router::RouteConstSharedPtr return_route,
                                      const std::string& return_destination) override;

    void resetDownstreamConnection() override{};

    StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }

    std::shared_ptr<Router::TransactionInfos> transactionInfos() override { return nullptr; }
    std::shared_ptr<SipProxy::DownstreamConnectionInfos> downstreamConnectionInfos() override {
      return nullptr;
    }
    std::shared_ptr<SipProxy::UpstreamTransactionInfos> upstreamTransactionInfo() override {
      return parent_.upstream_transaction_infos_;
    }

    void onReset() override{};

    std::shared_ptr<TrafficRoutingAssistantHandler> traHandler() override { return nullptr; }

    void continueHandling(const std::string& key, bool try_next_affinity) override {
      UNREFERENCED_PARAMETER(key);
      UNREFERENCED_PARAMETER(try_next_affinity);
    }

    MessageMetadataSharedPtr metadata() override { return nullptr; };

    // SipFilters::PendingListHandler
    void pushIntoPendingList(const std::string& type, const std::string& key,
                             SipFilters::DecoderFilterCallbacks& activetrans,
                             std::function<void(void)> func) override {
      UNREFERENCED_PARAMETER(type);
      UNREFERENCED_PARAMETER(key);
      UNREFERENCED_PARAMETER(activetrans);
      UNREFERENCED_PARAMETER(func);
    }
    void onResponseHandleForPendingList(
        const std::string& type, const std::string& key,
        std::function<void(MessageMetadataSharedPtr, DecoderEventHandler&)> func) override {
      UNREFERENCED_PARAMETER(type);
      UNREFERENCED_PARAMETER(key);
      UNREFERENCED_PARAMETER(func);
    }
    void eraseActiveTransFromPendingList(std::string& transaction_id) override {
      UNREFERENCED_PARAMETER(transaction_id);
    }

  private:
    ConnectionManager& parent_;
    StreamInfo::StreamInfoImpl stream_info_;
    // std::unique_ptr<UpstreamMessageDecoder> message_decoder_;
  };

  using ActiveTransPtr = std::unique_ptr<ActiveTrans>;

  void dispatch();
  void sendLocalReply(MessageMetadata& metadata, const DirectResponse& response, bool end_stream);
  void setLocalResponseSent(absl::string_view transaction_id);
  void sendUpstreamLocalReply(MessageMetadata& metadata, const DirectResponse& response,
                              bool end_stream);
  void doDeferredDownstreamTransDestroy(DownstreamActiveTrans& trans);
  void doDeferredUpstreamTransDestroy(UpstreamActiveTrans& trans);
  void resetAllTrans(bool local_reset);

  Config& config_;
  SipFilterStats& stats_;

  Network::ReadFilterCallbacks* read_callbacks_{};

  DecoderPtr decoder_;
  absl::flat_hash_map<std::string, ActiveTransPtr> transactions_;
  Buffer::OwnedImpl request_buffer_;
  Random::RandomGenerator& random_generator_;
  TimeSource& time_source_;
  Server::Configuration::FactoryContext& context_;

  std::shared_ptr<TrafficRoutingAssistantHandler> tra_handler_;

  absl::optional<OriginIngress> local_origin_ingress_;

  // This is used in Router, put here to pass to Router
  std::shared_ptr<Router::TransactionInfos> transaction_infos_;
  std::shared_ptr<SipProxy::DownstreamConnectionInfos> downstream_connection_infos_;
  std::shared_ptr<SipProxy::UpstreamTransactionInfos> upstream_transaction_infos_;
  PendingList pending_list_;
};

class DownstreamConnectionInfos : public std::enable_shared_from_this<DownstreamConnectionInfos>,
                                  Logger::Loggable<Logger::Id::connection> {
public:
  DownstreamConnectionInfos(ThreadLocal::SlotAllocator& tls) : tls_(tls.allocateSlot()) {}

  // init one threadlocal map per worker thread
  void init();
  ~DownstreamConnectionInfos() = default;

  void insertDownstreamConnection(std::string conn_id, ConnectionManager& conn_manager);
  size_t size();

  void deleteDownstreamConnection(std::string&& conn_id);

  bool hasDownstreamConnection(std::string& conn_id);
  SipFilters::DecoderFilterCallbacks& getDownstreamConnection(std::string& conn_id);

  std::string dumpDownstreamConnection();

private:
  ThreadLocal::SlotPtr tls_;
};

// TODO Idea for getting upstream transactions cache in TLS as common to all, review it

struct ThreadLocalUpstreamTransactionInfo : public ThreadLocal::ThreadLocalObject,
                                             public Logger::Loggable<Logger::Id::filter> {
  ThreadLocalUpstreamTransactionInfo(std::shared_ptr<UpstreamTransactionInfos> parent,
                                      Event::Dispatcher& dispatcher,
                                      std::chrono::milliseconds transaction_timeout)
      : parent_(parent), dispatcher_(dispatcher), transaction_timeout_(transaction_timeout) {
    if (!Thread::MainThread::isMainThread()) {
      audit_timer_ = dispatcher.createTimer([this]() -> void { auditTimerAction(); });
      audit_timer_->enableTimer(std::chrono::seconds(2));
    }
  }

  void auditTimerAction();

  absl::flat_hash_map<std::string, std::shared_ptr<ConnectionManager::UpstreamActiveTrans>>
      upstream_transaction_infos_map_{};

  std::shared_ptr<UpstreamTransactionInfos> parent_;
  Event::Dispatcher& dispatcher_;
  Event::TimerPtr audit_timer_;
  std::chrono::milliseconds transaction_timeout_;
};

class UpstreamTransactionInfos : public std::enable_shared_from_this<UpstreamTransactionInfos>,
                                 Logger::Loggable<Logger::Id::connection> {
public:
  UpstreamTransactionInfos(ThreadLocal::SlotAllocator& tls,
                           std::chrono::milliseconds transaction_timeout)
      : tls_(tls.allocateSlot()), transaction_timeout_(transaction_timeout) {}
  ~UpstreamTransactionInfos() = default;

  void init();

  void insertTransaction(std::string transaction_id,
                         std::shared_ptr<ConnectionManager::UpstreamActiveTrans> active_trans);
  void deleteTransaction(std::string&& transaction_id);
  bool hasTransaction(std::string& transaction_id);
  std::shared_ptr<ConnectionManager::UpstreamActiveTrans>
  getTransaction(std::string& transaction_id);
  size_t size();

private:
  ThreadLocal::SlotPtr tls_;
  std::chrono::milliseconds transaction_timeout_;
};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
