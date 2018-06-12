#pragma once

#include <fcntl.h>
#include <sys/un.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

#include "envoy/server/hot_restart.h"
#include "envoy/server/options.h"

#include "common/common/assert.h"
#include "common/common/block_memory_hash_set.h"
#include "common/stats/stats_impl.h"

namespace Envoy {
namespace Server {

typedef BlockMemoryHashSet<Stats::RawStatData> RawStatDataSet;

/**
 * Shared memory segment. This structure is laid directly into shared memory and is used amongst
 * all running envoy processes.
 */
class SharedMemory {
public:
  static void configure(uint64_t max_num_stats, uint64_t max_stat_name_len);
  static std::string version(uint64_t max_num_stats, uint64_t max_stat_name_len);

  // Made public for testing.
  static const uint64_t VERSION;

  int64_t maxStats() const { return max_stats_; }

private:
  struct Flags {
    static const uint64_t INITIALIZING = 0x1;
  };

  // Due to the flexible-array-length of stats_set_data_, c-style allocation
  // and initialization are neccessary.
  SharedMemory() = delete;
  ~SharedMemory() = delete;

  /**
   * Initialize the shared memory segment, depending on whether we should be the first running
   * envoy, or a host restarted envoy process.
   */
  static SharedMemory& initialize(uint64_t stats_set_size, Options& options);

  /**
   * Initialize a pthread mutex for process shared locking.
   */
  void initializeMutex(pthread_mutex_t& mutex);

  uint64_t size_;
  uint64_t version_;
  uint64_t max_stats_;
  uint64_t entry_size_;
  std::atomic<uint64_t> flags_;
  pthread_mutex_t log_lock_;
  pthread_mutex_t access_log_lock_;
  pthread_mutex_t stat_lock_;
  pthread_mutex_t init_lock_;
  alignas(BlockMemoryHashSet<Stats::RawStatData>) uint8_t stats_set_data_[];

  friend class HotRestartImpl;
};

/**
 * Implementation of Thread::BasicLockable that operates on a process shared pthread mutex.
 */
class ProcessSharedMutex : public Thread::BasicLockable {
public:
  ProcessSharedMutex(pthread_mutex_t& mutex) : mutex_(mutex) {}

  void lock() EXCLUSIVE_LOCK_FUNCTION() override {
    // Deal with robust handling here. If the other process dies without unlocking, we are going
    // to die shortly but try to make sure that we can handle any signals, etc. that happen without
    // getting into a further messed up state.
    int rc = pthread_mutex_lock(&mutex_);
    ASSERT(rc == 0 || rc == EOWNERDEAD);
    if (rc == EOWNERDEAD) {
      pthread_mutex_consistent(&mutex_);
    }
  }

  bool tryLock() EXCLUSIVE_TRYLOCK_FUNCTION(true) override {
    int rc = pthread_mutex_trylock(&mutex_);
    if (rc == EBUSY) {
      return false;
    }

    ASSERT(rc == 0 || rc == EOWNERDEAD);
    if (rc == EOWNERDEAD) {
      pthread_mutex_consistent(&mutex_);
    }

    return true;
  }

  void unlock() UNLOCK_FUNCTION() override {
    int rc = pthread_mutex_unlock(&mutex_);
    ASSERT(rc == 0);
  }

private:
  pthread_mutex_t& mutex_;
};

/**
 * Implementation of HotRestart built for Linux.
 */
class HotRestartImpl : public HotRestart,
                       public Stats::RawStatDataAllocator,
                       Logger::Loggable<Logger::Id::main> {
public:
  HotRestartImpl(Options& options);

  // Server::HotRestart
  void drainParentListeners() override;
  int duplicateParentListenSocket(const std::string& address) override;
  void getParentStats(GetParentStatsInfo& info) override;
  void initialize(Event::Dispatcher& dispatcher, Server::Instance& server) override;
  void shutdownParentAdmin(ShutdownParentAdminInfo& info) override;
  void terminateParent() override;
  void shutdown() override;
  std::string version() override;
  Thread::BasicLockable& logLock() override { return log_lock_; }
  Thread::BasicLockable& accessLogLock() override { return access_log_lock_; }
  Stats::StatDataAllocator& statsAllocator() override { return *this; }

  /**
   * envoy --hot_restart_version doesn't initialize Envoy, but computes the version string
   * based on the configured options.
   */
  static std::string hotRestartVersion(uint64_t max_num_stats, uint64_t max_stat_name_len);

  // RawStatDataAllocator
  Stats::RawStatData* alloc(const std::string& name) override;
  void free(Stats::RawStatData& data) override;

private:
  enum class RpcMessageType {
    DrainListenersRequest = 1,
    GetListenSocketRequest = 2,
    GetListenSocketReply = 3,
    ShutdownAdminRequest = 4,
    ShutdownAdminReply = 5,
    TerminateRequest = 6,
    UnknownRequestReply = 7,
    GetStatsRequest = 8,
    GetStatsReply = 9
  };

  struct RpcBase {
    RpcBase(RpcMessageType type, uint64_t length = sizeof(RpcBase))
        : type_(type), length_(length) {}

    RpcMessageType type_;
    uint64_t length_;
  } __attribute__((packed));

  struct RpcGetListenSocketRequest : public RpcBase {
    RpcGetListenSocketRequest() : RpcBase(RpcMessageType::GetListenSocketRequest, sizeof(*this)) {}

    char address_[256]{0};
  } __attribute__((packed));

  struct RpcGetListenSocketReply : public RpcBase {
    RpcGetListenSocketReply() : RpcBase(RpcMessageType::GetListenSocketReply, sizeof(*this)) {}

    int fd_{0};
  } __attribute__((packed));

  struct RpcShutdownAdminReply : public RpcBase {
    RpcShutdownAdminReply() : RpcBase(RpcMessageType::ShutdownAdminReply, sizeof(*this)) {}

    uint64_t original_start_time_{0};
  } __attribute__((packed));

  struct RpcGetStatsReply : public RpcBase {
    RpcGetStatsReply() : RpcBase(RpcMessageType::GetStatsReply, sizeof(*this)) {}

    uint64_t memory_allocated_{0};
    uint64_t num_connections_{0};
    uint64_t unused_[16]{0};
  } __attribute__((packed));

  template <class rpc_class, RpcMessageType rpc_type> rpc_class* receiveTypedRpc() {
    RpcBase* base_message = receiveRpc(true);
    RELEASE_ASSERT(base_message->length_ == sizeof(rpc_class));
    RELEASE_ASSERT(base_message->type_ == rpc_type);
    return reinterpret_cast<rpc_class*>(base_message);
  }

  int bindDomainSocket(uint64_t id);
  void initDomainSocketAddress(sockaddr_un* address);
  sockaddr_un createDomainSocketAddress(uint64_t id);
  void onGetListenSocket(RpcGetListenSocketRequest& rpc);
  void onSocketEvent();
  RpcBase* receiveRpc(bool block);
  void sendMessage(sockaddr_un& address, RpcBase& rpc);
  static std::string versionHelper(uint64_t max_num_stats, uint64_t max_stat_name_len,
                                   RawStatDataSet& stats_set);

  Options& options_;
  BlockMemoryHashSetOptions stats_set_options_;
  SharedMemory& shmem_;
  std::unique_ptr<RawStatDataSet> stats_set_ GUARDED_BY(stat_lock_);
  ProcessSharedMutex log_lock_;
  ProcessSharedMutex access_log_lock_;
  ProcessSharedMutex stat_lock_;
  ProcessSharedMutex init_lock_;
  int my_domain_socket_{-1};
  sockaddr_un parent_address_;
  sockaddr_un child_address_;
  Event::FileEventPtr socket_event_;
  std::array<uint8_t, 4096> rpc_buffer_;
  Server::Instance* server_{};
  bool parent_terminated_{};
};

} // namespace Server
} // namespace Envoy
