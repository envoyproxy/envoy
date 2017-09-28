#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <vector>

#include "envoy/event/deferred_deletable.h"
#include "envoy/event/dispatcher.h"
#include "envoy/network/connection_handler.h"

#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/event/libevent.h"

namespace Envoy {
namespace Event {

/**
 * Class to manage child processes and termination signals.
 */
class ChildManager {
public:
  ChildManager(OsSysCallsPtr syscalls);

  /**
   * fork() and exec() the specified process with args.  Associate the child pid with
   * the callback so the caller will be notified of termination.
   *
   * @see Dispatcher::runProcess() for parameters.
   */
  ChildProcessPtr run(const std::vector<std::string>& args, ProcessTerminationCb&& cb);

  /**
   * Tell the manager that a SIGCHLD was received and that it should ask the operating
   * system which PID had an event.
   */
  void onSigChld();

  /**
   * Returns true iff pid is currently being watched for termination.
   */
  bool watched(pid_t pid) const { return processes_.find(pid) != processes_.end(); }

  /**
   * Returns the number of pids being watched.
   */
  size_t numWatched() const { return processes_.size(); }

private:
  /**
   * Implementation of ChildProcess which causes a child process to be sent
   * SIGTERM when it's destructor is called, unless the process already
   * terminated.
   */
  struct ChildProcessImpl : public ChildProcess {
    ChildProcessImpl(ChildManager& parent, pid_t pid) : parent_(parent), pid_(pid) {}
    virtual ~ChildProcessImpl() { parent_.remove(pid_, this); }

    ChildManager& parent_;
    const pid_t pid_;
  };

  /**
   * Remove pid from the set of watched processes and send the process
   * SIGTERM if it is still running.
   */
  void remove(pid_t pid, ChildProcessImpl* cancellation);

  OsSysCallsPtr syscalls_;
  std::unordered_map<pid_t, std::pair<ProcessTerminationCb, ChildProcessImpl*>> processes_;
};

/**
 * libevent implementation of Event::Dispatcher.
 */
class DispatcherImpl : Logger::Loggable<Logger::Id::main>, public Dispatcher {
public:
  DispatcherImpl();
  DispatcherImpl(Buffer::WatermarkFactoryPtr&& factory);
  ~DispatcherImpl();

  /**
   * @return event_base& the libevent base.
   */
  event_base& base() { return *base_; }

  // Event::Dispatcher
  void clearDeferredDeleteList() override;
  Network::ClientConnectionPtr
  createClientConnection(Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address) override;
  Network::ClientConnectionPtr
  createSslClientConnection(Ssl::ClientContext& ssl_ctx,
                            Network::Address::InstanceConstSharedPtr address,
                            Network::Address::InstanceConstSharedPtr source_address) override;
  Network::DnsResolverSharedPtr createDnsResolver(
      const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers) override;
  FileEventPtr createFileEvent(int fd, FileReadyCb cb, FileTriggerType trigger,
                               uint32_t events) override;
  Filesystem::WatcherPtr createFilesystemWatcher() override;
  Network::ListenerPtr createListener(Network::ConnectionHandler& conn_handler,
                                      Network::ListenSocket& socket, Network::ListenerCallbacks& cb,
                                      Stats::Scope& scope,
                                      const Network::ListenerOptions& listener_options) override;
  Network::ListenerPtr createSslListener(Network::ConnectionHandler& conn_handler,
                                         Ssl::ServerContext& ssl_ctx, Network::ListenSocket& socket,
                                         Network::ListenerCallbacks& cb, Stats::Scope& scope,
                                         const Network::ListenerOptions& listener_options) override;
  TimerPtr createTimer(TimerCb cb) override;
  void deferredDelete(DeferredDeletablePtr&& to_delete) override;
  void exit() override;
  SignalEventPtr listenForSignal(int signal_num, SignalCb cb) override;
  ChildProcessPtr runProcess(const std::vector<std::string>& args,
                             ProcessTerminationCb cb) override;
  void post(std::function<void()> callback) override;
  void run(RunType type) override;
  Buffer::WatermarkFactory& getWatermarkFactory() override { return *buffer_factory_; }

private:
  void runPostCallbacks();
#ifndef NDEBUG
  // Validate that an operation is thread safe, i.e. it's invoked on the same thread that the
  // dispatcher run loop is executing on. We allow run_tid_ == 0 for tests where we don't invoke
  // run().
  bool isThreadSafe() const {
    return run_tid_ == 0 || run_tid_ == Thread::Thread::currentThreadId();
  }
#endif

  Thread::ThreadId run_tid_{};
  Buffer::WatermarkFactoryPtr buffer_factory_;
  Libevent::BasePtr base_;
  TimerPtr deferred_delete_timer_;
  TimerPtr post_timer_;
  std::vector<DeferredDeletablePtr> to_delete_1_;
  std::vector<DeferredDeletablePtr> to_delete_2_;
  std::vector<DeferredDeletablePtr>* current_to_delete_;
  std::mutex post_lock_;
  std::list<std::function<void()>> post_callbacks_;
  bool deferred_deleting_{};
  ChildManager child_manager_;
  Event::SignalEventPtr sigchld_;
};

} // namespace Event
} // namespace Envoy
