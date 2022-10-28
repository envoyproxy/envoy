#pragma once

#include <cstddef>

#include "envoy/buffer/buffer.h"
#include "envoy/server/factory_context.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/protobuf/protobuf.h"

#include "contrib/envoy/extensions/filters/network/sip_proxy/tra/v3alpha/tra.pb.h"
#include "contrib/envoy/extensions/filters/network/sip_proxy/v3alpha/sip_proxy.pb.h"
#include "contrib/sip_proxy/filters/network/source/decoder_events.h"
#include "contrib/sip_proxy/filters/network/source/metadata.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {
namespace SipFilters {
class DecoderFilterCallbacks;
}

class SipSettings {
public:
  SipSettings(
      std::chrono::milliseconds transaction_timeout,
      const Protobuf::RepeatedPtrField<
          envoy::extensions::filters::network::sip_proxy::v3alpha::LocalService>& local_services,
      const envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceConfig&
          tra_service_config,
      bool operate_via)
      : transaction_timeout_(transaction_timeout), tra_service_config_(tra_service_config),
        operate_via_(operate_via) {
    UNREFERENCED_PARAMETER(operate_via_);

    for (const auto& service : local_services) {
      local_services_.emplace_back(service);
    }
  }

  SipSettings(
      envoy::extensions::filters::network::sip_proxy::v3alpha::SipProxy::SipSettings sip_settings) {
    transaction_timeout_ = static_cast<std::chrono::milliseconds>(
        PROTOBUF_GET_MS_OR_DEFAULT(sip_settings, transaction_timeout, 32000));
    tra_service_config_ = sip_settings.tra_service_config();
    operate_via_ = sip_settings.operate_via();
    for (const auto& service : sip_settings.local_services()) {
      local_services_.emplace_back(service);
    }
  }

  std::chrono::milliseconds transactionTimeout() { return transaction_timeout_; }
  std::vector<envoy::extensions::filters::network::sip_proxy::v3alpha::LocalService>&
  localServices() {
    return local_services_;
  }
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceConfig&
  traServiceConfig() {
    return tra_service_config_;
  }

private:
  std::chrono::milliseconds transaction_timeout_;

  std::vector<envoy::extensions::filters::network::sip_proxy::v3alpha::LocalService>
      local_services_;
  envoy::extensions::filters::network::sip_proxy::tra::v3alpha::TraServiceConfig
      tra_service_config_;
  bool operate_via_;
};

/**
 * A DirectResponse manipulates a Protocol to directly create a Sip response message.
 */
class DirectResponse {
public:
  virtual ~DirectResponse() = default;

  enum class ResponseType {
    // DirectResponse encodes MessageType::Reply with success payload
    SuccessReply,

    // DirectResponse encodes MessageType::Reply with an exception payload
    ErrorReply,

    // DirectResponse encodes MessageType::Exception
    Exception,
  };

  /**
   * Encodes the response via the given Protocol.
   * @param metadata the MessageMetadata for the request that generated this response
   * @param proto the Protocol to be used for message encoding
   * @param buffer the Buffer into which the message should be encoded
   * @return ResponseType indicating whether the message is a successful or error reply or an
   *         exception
   */
  virtual ResponseType encode(MessageMetadata& metadata, Buffer::Instance& buffer) const PURE;
};

/**
 * In order to handle TRA retrieve async, introduce PendingList to hold current message.
 * described as below:
 *
 * --> tra_query_request
 *     --> has local cache
 *         --> pending_list[base_uri] has value
 *             --> hold current message into pending_list with base_uri as key
 *         --> pending_list[base_uri] no value
 *             --> do tra query
 *             --> hold current message into pending_list with base_uri as key
 *     --> no  local cache
 *         --> full_uri as key
 *         --> hold current message into pending_list;
 *
 * --> tra_query_response_arrived
 *     --> handle all messages in same query
 *         --> continue_to_handle
 */
class PendingList : public Logger::Loggable<Logger::Id::filter> {
public:
  PendingList() = default;

  void pushIntoPendingList(const std::string& type, const std::string& key,
                           SipFilters::DecoderFilterCallbacks& activetrans,
                           std::function<void(void)> func);

  // TODO this should be enhanced to save index in hash table keyed with
  // transaction_id to improve search performance
  void eraseActiveTransFromPendingList(std::string& transaction_id);

  void onResponseHandleForPendingList(
      const std::string& type, const std::string& key,
      std::function<void(MessageMetadataSharedPtr, DecoderEventHandler&)> func);

private:
  absl::flat_hash_map<std::string,
                      std::list<std::reference_wrapper<SipFilters::DecoderFilterCallbacks>>>
      pending_list_;
};

class PendingListHandler {
public:
  virtual ~PendingListHandler() = default;
  virtual void pushIntoPendingList(const std::string& type, const std::string& key,
                                   SipFilters::DecoderFilterCallbacks& activetrans,
                                   std::function<void(void)> func) PURE;
  virtual void onResponseHandleForPendingList(
      const std::string& type, const std::string& key,
      std::function<void(MessageMetadataSharedPtr, DecoderEventHandler&)> func) PURE;
  virtual void eraseActiveTransFromPendingList(std::string& transaction_id) PURE;
};

class Utility {
public:
  static const std::string& localAddress(Server::Configuration::FactoryContext& context) {
    return context.getTransportSocketFactoryContext()
        .localInfo()
        .address()
        ->ip()
        ->addressAsString();
  }
};

/**
 * flat_hash_map with size limitation.
 * @tparam max_size: the maximum size of the hash map, default is 1024.
 */
template <typename K, typename V> class Cache {
public:
  Cache(unsigned int max_size = 1024)
      : max_size_(max_size), ring_buffer_(max_size_), it_(ring_buffer_.begin()) {}

  Cache(const Cache&) = delete;
  Cache(const Cache&&) = delete;
  Cache& operator=(const Cache&) = delete;
  virtual ~Cache() = default;

  void emplace(const K& key, const V& value) {
    if (contains(key)) {
      if (it_ != cache_[key].it_) {
        ring_buffer_.erase(cache_[key].it_);
        auto new_it = ring_buffer_.insert(it_, key);
        cache_[key].it_ = new_it;
      }
      cache_[key].value_ = value;
    } else {
      if (!it_->empty()) {
        cache_.erase(*it_);
      }

      *it_ = key;

      Data d{value, it_};
      cache_.emplace(key, d);

      ++it_;
    }

    if (it_ == ring_buffer_.end()) {
      it_ = ring_buffer_.begin();
    }
  }

  bool contains(const K& key) { return cache_.find(key) != cache_.end(); }

  void erase(const K& key) {
    if (contains(key)) {
      *(cache_[key].it_) = "";
      cache_.erase(key);
    }
  }

  V& operator[](const K& key) {
    if (contains(key)) {
      return cache_[key].value_;
    } else {
      return default_value_;
    }
  }

private:
  struct Data {
    V value_;
    typename std::list<K>::iterator it_;
  };

  unsigned int max_size_;
  std::map<K, Data> cache_;
  std::list<K> ring_buffer_;
  typename std::list<K>::iterator it_;

  V default_value_{};
};

template <typename T, typename K, typename V> class CacheManager {
public:
  /**
   * @brief Construct a new Cache Manager object
   *
   * @param max_size: the maximum size of each cache type, default is 1024.
   */
  CacheManager(unsigned int max_size = 1024) : max_size_(max_size) {}

  void initCache(const T& type, unsigned int max_size = 1024) { caches_.emplace(type, max_size); }

  void insertCache(const T& type, const K& key, const V& value) {
    if (caches_.find(type) != caches_.end()) {
      // TODO if (caches_.contains(type)) {
      caches_[type].emplace(key, value);
    } else {
      caches_.emplace(type, max_size_);
      caches_[type].emplace(key, value);
    }
  }

  bool contains(const T& type, const K& key) {
    // TODO return caches_.contains(type) && caches_[type].contains(key);
    return caches_.find(type) != caches_.end() && caches_[type].contains(key);
  }
  V& at(const T& type, const K& key) { return caches_[type][key]; }
  Cache<K, V>& at(const T& type) { return caches_[type]; }
  Cache<K, V>& operator[](const T& type) { return caches_[type]; }

private:
  // Maximum size of each cache type.
  unsigned int max_size_;
  std::map<T, Cache<K, V>> caches_;
};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
