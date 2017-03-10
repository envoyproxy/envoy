#pragma once

#include "envoy/server/hot_restart.h"
#include "envoy/server/options.h"

#include "common/common/assert.h"
#include "common/stats/stats_impl.h"

#include <fcntl.h>
#include <sys/un.h>

namespace Server {

/**
 * Shared memory segment. This structure is laid directly into shared memory and is used amongst
 * all running envoy processes.
 */
class SharedMemory {
public:
  static std::string version();

private:
  struct Flags {
    static const uint64_t INITIALIZING = 0x1;
  };

  SharedMemory() {}

  /**
   * Initialize the shared memory segment, depending on whether we should be the first running
   * envoy, or a host restarted envoy process.
   */
  static SharedMemory& initialize(Options& options);

  /**
   * Initialize a pthread mutex for process shared locking.
   */
  void initializeMutex(pthread_mutex_t& mutex);

  static const uint64_t VERSION;

  uint64_t size_;
  uint64_t version_;
  std::atomic<uint64_t> flags_;
  pthread_mutex_t log_lock_;
  pthread_mutex_t access_log_lock_;
  pthread_mutex_t stat_lock_;
  std::array<Stats::RawStatData, 16384> stats_slots_;

  friend class HotRestartImpl;
};

/**
 * Implementation of Thread::BasicLockable that operates on a process shared pthread mutex.
 */
class ProcessSharedMutex : public Thread::BasicLockable {
public:
  ProcessSharedMutex(pthread_mutex_t& mutex) : mutex_(mutex) {}

  void lock() override {
    // Deal with robust handling here. If the other process dies without unlocking, we are going
    // to die shortly but try to make sure that we can handle any signals, etc. that happen without
    // getting into a further messed up state.
    int rc = pthread_mutex_lock(&mutex_);
    ASSERT(rc == 0 || rc == EOWNERDEAD);
    if (rc == EOWNERDEAD) {
      pthread_mutex_consistent(&mutex_);
    }
  }

  void unlock() override {
    int rc = pthread_mutex_unlock(&mutex_);
    ASSERT(rc == 0);
    UNREFERENCED_PARAMETER(rc);
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

  Thread::BasicLockable& logLock() { return log_lock_; }
  Thread::BasicLockable& accessLogLock() { return access_log_lock_; }

  // Server::HotRestart
  void drainParentListeners() override;
  int duplicateParentListenSocket(uint32_t port) override;
  void getParentStats(GetParentStatsInfo& info) override;
  void initialize(Event::Dispatcher& dispatcher, Server::Instance& server) override;
  void shutdownParentAdmin(ShutdownParentAdminInfo& info) override;
  void terminateParent() override;
  std::string version() override;

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
  };

  struct RpcGetListenSocketRequest : public RpcBase {
    RpcGetListenSocketRequest() : RpcBase(RpcMessageType::GetListenSocketRequest, sizeof(*this)) {}

    uint32_t port_;
  };

  struct RpcGetListenSocketReply : public RpcBase {
    RpcGetListenSocketReply() : RpcBase(RpcMessageType::GetListenSocketReply, sizeof(*this)) {}

    int fd_;
  };

  struct RpcShutdownAdminReply : public RpcBase {
    RpcShutdownAdminReply() : RpcBase(RpcMessageType::ShutdownAdminReply, sizeof(*this)) {}

    uint64_t original_start_time_;
  };

  struct RpcGetStatsReply : public RpcBase {
    RpcGetStatsReply() : RpcBase(RpcMessageType::GetStatsReply, sizeof(*this)) {}

    uint64_t memory_allocated_;
    uint64_t num_connections_;
    uint64_t unused_[16];
  };

  template <class rpc_class, RpcMessageType rpc_type> rpc_class* receiveTypedRpc() {
    RpcBase* base_message = receiveRpc(true);
    RELEASE_ASSERT(base_message->length_ == sizeof(rpc_class));
    RELEASE_ASSERT(base_message->type_ == rpc_type);
    return reinterpret_cast<rpc_class*>(base_message);
  }

  int bindDomainSocket(uint64_t id);
  sockaddr_un createDomainSocketAddress(uint64_t id);
  void onGetListenSocket(RpcGetListenSocketRequest& rpc);
  void onSocketEvent();
  RpcBase* receiveRpc(bool block);
  void sendMessage(sockaddr_un& address, RpcBase& rpc);

  Options& options_;
  SharedMemory& shmem_;
  ProcessSharedMutex log_lock_;
  ProcessSharedMutex access_log_lock_;
  ProcessSharedMutex stat_lock_;
  int my_domain_socket_{-1};
  sockaddr_un parent_address_;
  sockaddr_un child_address_;
  Event::FileEventPtr socket_event_;
  std::array<uint8_t, 4096> rpc_buffer_;
  Server::Instance* server_{};
  bool parent_terminated_{};
};

} // Server
