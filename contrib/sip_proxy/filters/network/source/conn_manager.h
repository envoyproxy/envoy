#pragma once

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

class ConnectionManager;
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
  ConnectionManager(Config& config, Random::RandomGenerator& random_generator,
                    TimeSource& time_system, Server::Configuration::FactoryContext& context,
                    std::shared_ptr<Router::TransactionInfos> transaction_infos);
  ~ConnectionManager() override;

  // Network::ReadFilter
  Network::FilterStatus onData(Buffer::Instance& data, bool end_stream) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }
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
  struct ActiveTrans;

  struct ResponseDecoder : public DecoderCallbacks, public DecoderEventHandler {
    ResponseDecoder(ActiveTrans& parent) : parent_(parent) {}

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
  using ResponseDecoderPtr = std::unique_ptr<ResponseDecoder>;

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
    Router::RouteConstSharedPtr route() override { return parent_.route(); }
    SipFilterStats& stats() override { return parent_.stats(); }
    void sendLocalReply(const DirectResponse& response, bool end_stream) override {
      parent_.sendLocalReply(response, end_stream);
    }
    void startUpstreamResponse() override { parent_.startUpstreamResponse(); }
    SipFilters::ResponseStatus upstreamData(MessageMetadataSharedPtr metadata) override {
      return parent_.upstreamData(metadata);
    }
    void resetDownstreamConnection() override { parent_.resetDownstreamConnection(); }
    StreamInfo::StreamInfo& streamInfo() override { return parent_.streamInfo(); }
    std::shared_ptr<Router::TransactionInfos> transactionInfos() override {
      return parent_.transactionInfos();
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
        : parent_(parent), request_timer_(new Stats::HistogramCompletableTimespanImpl(
                               parent_.stats_.request_time_ms_, parent_.time_source_)),
          stream_id_(parent_.random_generator_.random()),
          transaction_id_(metadata->transactionId().value()),
          stream_info_(parent_.time_source_,
                       parent_.read_callbacks_->connection().connectionInfoProviderSharedPtr()),
          metadata_(metadata) {
      parent.stats_.request_active_.inc();
    }
    ~ActiveTrans() override {
      request_timer_->complete();
      parent_.stats_.request_active_.dec();

      parent_.eraseActiveTransFromPendingList(transaction_id_);
      for (auto& filter : decoder_filters_) {
        filter->handle_->onDestroy();
      }
    }

    // DecoderEventHandler
    FilterStatus transportBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus transportEnd() override;
    FilterStatus messageBegin(MessageMetadataSharedPtr metadata) override;
    FilterStatus messageEnd() override;

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
    uint64_t streamId() const override { return stream_id_; }
    std::string transactionId() const override { return transaction_id_; }
    const Network::Connection* connection() const override;
    Router::RouteConstSharedPtr route() override;
    SipFilterStats& stats() override { return parent_.stats_; }
    void sendLocalReply(const DirectResponse& response, bool end_stream) override;
    void startUpstreamResponse() override;
    SipFilters::ResponseStatus upstreamData(MessageMetadataSharedPtr metadata) override;
    void resetDownstreamConnection() override;
    StreamInfo::StreamInfo& streamInfo() override { return stream_info_; }

    std::shared_ptr<Router::TransactionInfos> transactionInfos() override {
      return parent_.transaction_infos_;
    }
    std::shared_ptr<SipSettings> settings() const override { return parent_.config_.settings(); }
    void onReset() override;
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
    void onError(const std::string& what);
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
    ResponseDecoderPtr response_decoder_;
    absl::optional<Router::RouteConstSharedPtr> cached_route_;

    std::function<FilterStatus(DecoderEventHandler*)> filter_action_;

    absl::any filter_context_;
    bool local_response_sent_{false};

    /* Used by Router */
    std::shared_ptr<Router::TransactionInfos> transaction_infos_;
  };

  using ActiveTransPtr = std::unique_ptr<ActiveTrans>;

  void dispatch();
  void sendLocalReply(MessageMetadata& metadata, const DirectResponse& response, bool end_stream);
  void setLocalResponseSent(absl::string_view transaction_id);
  void doDeferredTransDestroy(ActiveTrans& trans);
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

  // This is used in Router, put here to pass to Router
  std::shared_ptr<Router::TransactionInfos> transaction_infos_;
  PendingList pending_list_;
};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
