#pragma once

#include "extensions/filters/http/cache/hazelcast_http_cache/hazelcast_http_cache.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace HazelcastHttpCache {

/**
 * Abstraction for the accessors used in tests.
 *
 * Contains pure functions to obtain storage information, modify the storage and
 * change accessor behavior directly.
 */
class TestAccessor {
public:
  TestAccessor() = default;

  virtual void clearMaps() PURE;
  virtual void dropConnection() PURE;
  virtual void restoreConnection() PURE;

  virtual int headerMapSize() PURE;
  virtual int bodyMapSize() PURE;
  virtual int responseMapSize() PURE;

  virtual void insertResponse(int64_t map_key, const HazelcastResponseEntry& entry) PURE;
  virtual void removeBody(const std::string& map_key) PURE;

  virtual void failOnLock() PURE;

  virtual ~TestAccessor() = default;
};

/**
 * Testable Hazelcast cluster accessor.
 *
 * @note A Hazelcast instance must be up during tests when this accessor is used.
 */
class RemoteTestAccessor : public TestAccessor, public HazelcastClusterAccessor {
public:
  RemoteTestAccessor(HazelcastHttpCache& cache, ClientConfig&& client_config,
                     const std::string& app_prefix, const uint64_t partition_size)
      : HazelcastClusterAccessor(cache, std::move(client_config), app_prefix, partition_size){};

  void clearMaps() override {
    getResponseMap().clear();
    getBodyMap().clear();
    getHeaderMap().clear();
  }

  void dropConnection() override { disconnect(); }

  void restoreConnection() override { connect(); }

  int headerMapSize() override { return getHeaderMap().size(); }

  int bodyMapSize() override { return getBodyMap().size(); }

  int responseMapSize() override { return getResponseMap().size(); }

  void removeBody(const std::string& map_key) override { getBodyMap().remove(map_key); }

  void insertResponse(int64_t map_key, const HazelcastResponseEntry& entry) override {
    getResponseMap().put(map_key, entry);
  }

  void failOnLock() override {} // Required for local accessor only.
};

/**
 * Testable local storage accessor.
 *
 * @note This accessor does not use any Hazelcast instance during tests.
 * Instead, it simulates Hazelcast instance with local storage.
 */
class LocalTestAccessor : public StorageAccessor, public TestAccessor {
public:
  LocalTestAccessor() = default;

  // TestAccessor
  void clearMaps() override {
    header_map_.clear();
    body_map_.clear();
    response_map_.clear();
  }

  void dropConnection() override { disconnect(); }

  void restoreConnection() override { connect(); }

  int headerMapSize() override { return header_map_.size(); }

  int bodyMapSize() override { return body_map_.size(); }

  int responseMapSize() override { return response_map_.size(); }

  void insertResponse(int64_t map_key, const HazelcastResponseEntry& entry) override {
    checkConnection();
    response_map_[map_key] = HazelcastResponsePtr(new HazelcastResponseEntry(entry));
  }

  void removeBody(const std::string& map_key) override {
    checkConnection();
    removeBodyAsync(map_key);
  }

  // StorageAccessor
  void putHeader(const int64_t map_key, const HazelcastHeaderEntry& value) override {
    checkConnection();
    header_map_[map_key] = HazelcastHeaderPtr(new HazelcastHeaderEntry(value));
  }

  void putBody(const std::string& map_key, const HazelcastBodyEntry& value) override {
    checkConnection();
    body_map_[map_key] = HazelcastBodyPtr(new HazelcastBodyEntry(value));
  }

  void putResponse(const int64_t map_key, const HazelcastResponseEntry& value) override {
    insertResponse(map_key, value);
  }

  HazelcastHeaderPtr getHeader(const int64_t map_key) override {
    checkConnection();
    auto result = header_map_.find(map_key);
    if (result != header_map_.end()) {
      // New objects are created during deserialization. Hence not returning
      // the original one here.
      return HazelcastHeaderPtr(new HazelcastHeaderEntry(*result->second));
    } else {
      return nullptr;
    }
  }

  HazelcastBodyPtr getBody(const std::string& map_key) override {
    checkConnection();
    auto result = body_map_.find(map_key);
    if (result != body_map_.end()) {
      return HazelcastBodyPtr(new HazelcastBodyEntry(*result->second));
    } else {
      return nullptr;
    }
  }

  HazelcastResponsePtr getResponse(const int64_t map_key) override {
    checkConnection();
    auto result = response_map_.find(map_key);
    if (result != response_map_.end()) {
      return HazelcastResponsePtr(new HazelcastResponseEntry(*result->second));
    } else {
      return nullptr;
    }
  }

  void removeBodyAsync(const std::string& map_key) override {
    checkConnection();
    body_map_.erase(map_key);
  }

  void removeHeader(const int64_t map_key) override {
    checkConnection();
    header_map_.erase(map_key);
  }

  bool tryLock(const int64_t map_key, bool unified) override {
    checkConnection(fail_on_lock_);
    if (unified) {
      bool locked = std::find(response_locks_.begin(), response_locks_.end(), map_key) !=
                    response_locks_.end();
      if (locked) {
        return false;
      } else {
        response_locks_.push_back(map_key);
        return true;
      }
    } else {
      bool locked =
          std::find(header_locks_.begin(), header_locks_.end(), map_key) != header_locks_.end();
      if (locked) {
        return false;
      } else {
        header_locks_.push_back(map_key);
        return true;
      }
    }
  }

  void unlock(const int64_t map_key, bool unified) override {
    checkConnection();
    if (unified) {
      response_locks_.erase(std::remove(response_locks_.begin(), response_locks_.end(), map_key),
                            response_locks_.end());
    } else {
      header_locks_.erase(std::remove(header_locks_.begin(), header_locks_.end(), map_key),
                          header_locks_.end());
    }
  }

  bool isRunning() const override { return connected_; }

  std::string clusterName() const override { return "LocalTestAccessor"; }

  std::string startInfo() const override { return ""; }

  void connect() override { connected_ = true; }

  void disconnect() override { connected_ = false; }

  void failOnLock() override { fail_on_lock_ = true; }

private:
  void checkConnection(bool force_fail = false) {
    if (!connected_ || force_fail) {
      // Different exceptions are thrown for consecutive fails to test other catch behaviors.
      switch (exception_counter_++ % 3) {
      case 0:
        throw std::exception();
      case 1:
        throw hazelcast::client::exception::HazelcastClientOfflineException(
            "LocalTestAccessor::checkConnection", "Hazelcast client is offline");
      default:
        throw hazelcast::client::exception::OperationTimeoutException(
            "LocalTestAccessor::checkConnection", "Operation timed out");
      }
    }
  }

  std::unordered_map<int64_t, HazelcastHeaderPtr> header_map_;
  std::unordered_map<std::string, HazelcastBodyPtr> body_map_;
  std::unordered_map<int64_t, HazelcastResponsePtr> response_map_;

  std::vector<uint64_t> header_locks_;
  std::vector<uint64_t> response_locks_;

  bool connected_ = false;
  bool fail_on_lock_ = false;
  int exception_counter_ = 0;
};

} // namespace HazelcastHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
