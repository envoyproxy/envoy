#include "common/event/dispatcher_impl.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"

#include "common/buffer/buffer_impl.h"
#include "common/event/file_event_impl.h"
#include "common/event/signal_impl.h"
#include "common/event/timer_impl.h"
#include "common/filesystem/watcher_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/dns_impl.h"
#include "common/network/listener_impl.h"
#include "common/ssl/connection_impl.h"

#include "event2/event.h"

namespace Envoy {
namespace Event {

class OsSysCallsImpl : public OsSysCalls {
  pid_t fork() override { return ::fork(); }

  int execvp(const char* file, char* const argv[]) override { return ::execvp(file, argv); }

  pid_t waitpid(pid_t pid, WaitpidStatus& status, int options) override {
    int status_int = 0;
    const pid_t ret = ::waitpid(pid, &status_int, options);

    status.signalled_ = WIFSIGNALED(status_int);
    status.exited_ = WIFEXITED(status_int);
    status.exit_status_ = WEXITSTATUS(status_int);

    return ret;
  }

  int kill(pid_t pid, int sig) override { return ::kill(pid, sig); }

  void _exit(int status) override { ::_exit(status); }

  int open(const char* pathname, int flags) override { return ::open(pathname, flags); }

  int dup2(int oldfd, int newfd) override { return ::dup2(oldfd, newfd); }

  int close(int fd) override { return ::close(fd); }
};

DispatcherImpl::DispatcherImpl()
    : DispatcherImpl(Buffer::WatermarkFactoryPtr{new Buffer::WatermarkBufferFactory}) {}

DispatcherImpl::DispatcherImpl(Buffer::WatermarkFactoryPtr&& factory)
    : buffer_factory_(std::move(factory)), base_(event_base_new()),
      deferred_delete_timer_(createTimer([this]() -> void { clearDeferredDeleteList(); })),
      post_timer_(createTimer([this]() -> void { runPostCallbacks(); })),
      current_to_delete_(&to_delete_1_), child_manager_(OsSysCallsPtr(new OsSysCallsImpl)) {}

DispatcherImpl::~DispatcherImpl() {}

void DispatcherImpl::clearDeferredDeleteList() {
  ASSERT(isThreadSafe());
  std::vector<DeferredDeletablePtr>* to_delete = current_to_delete_;

  size_t num_to_delete = to_delete->size();
  if (deferred_deleting_ || !num_to_delete) {
    return;
  }

  ENVOY_LOG(trace, "clearing deferred deletion list (size={})", num_to_delete);

  // Swap the current deletion vector so that if we do deferred delete while we are deleting, we
  // use the other vector. We will get another callback to delete that vector.
  if (current_to_delete_ == &to_delete_1_) {
    current_to_delete_ = &to_delete_2_;
  } else {
    current_to_delete_ = &to_delete_1_;
  }

  deferred_deleting_ = true;

  // Calling clear() on the vector does not specify which order destructors run in. We want to
  // destroy in FIFO order so just do it manually. This required 2 passes over the vector which is
  // not optimal but can be cleaned up later if needed.
  for (size_t i = 0; i < num_to_delete; i++) {
    (*to_delete)[i].reset();
  }

  to_delete->clear();
  deferred_deleting_ = false;
}

Network::ClientConnectionPtr
DispatcherImpl::createClientConnection(Network::Address::InstanceConstSharedPtr address,
                                       Network::Address::InstanceConstSharedPtr source_address) {
  ASSERT(isThreadSafe());
  return Network::ClientConnectionPtr{
      new Network::ClientConnectionImpl(*this, address, source_address)};
}

Network::ClientConnectionPtr
DispatcherImpl::createSslClientConnection(Ssl::ClientContext& ssl_ctx,
                                          Network::Address::InstanceConstSharedPtr address,
                                          Network::Address::InstanceConstSharedPtr source_address) {
  ASSERT(isThreadSafe());
  return Network::ClientConnectionPtr{
      new Ssl::ClientConnectionImpl(*this, ssl_ctx, address, source_address)};
}

Network::DnsResolverSharedPtr DispatcherImpl::createDnsResolver(
    const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers) {
  ASSERT(isThreadSafe());
  return Network::DnsResolverSharedPtr{new Network::DnsResolverImpl(*this, resolvers)};
}

FileEventPtr DispatcherImpl::createFileEvent(int fd, FileReadyCb cb, FileTriggerType trigger,
                                             uint32_t events) {
  ASSERT(isThreadSafe());
  return FileEventPtr{new FileEventImpl(*this, fd, cb, trigger, events)};
}

Filesystem::WatcherPtr DispatcherImpl::createFilesystemWatcher() {
  ASSERT(isThreadSafe());
  return Filesystem::WatcherPtr{new Filesystem::WatcherImpl(*this)};
}

Network::ListenerPtr
DispatcherImpl::createListener(Network::ConnectionHandler& conn_handler,
                               Network::ListenSocket& socket, Network::ListenerCallbacks& cb,
                               Stats::Scope& scope,
                               const Network::ListenerOptions& listener_options) {
  ASSERT(isThreadSafe());
  return Network::ListenerPtr{
      new Network::ListenerImpl(conn_handler, *this, socket, cb, scope, listener_options)};
}

Network::ListenerPtr
DispatcherImpl::createSslListener(Network::ConnectionHandler& conn_handler,
                                  Ssl::ServerContext& ssl_ctx, Network::ListenSocket& socket,
                                  Network::ListenerCallbacks& cb, Stats::Scope& scope,
                                  const Network::ListenerOptions& listener_options) {
  ASSERT(isThreadSafe());
  return Network::ListenerPtr{new Network::SslListenerImpl(conn_handler, *this, ssl_ctx, socket, cb,
                                                           scope, listener_options)};
}

TimerPtr DispatcherImpl::createTimer(TimerCb cb) {
  ASSERT(isThreadSafe());
  return TimerPtr{new TimerImpl(*this, cb)};
}

void DispatcherImpl::deferredDelete(DeferredDeletablePtr&& to_delete) {
  ASSERT(isThreadSafe());
  current_to_delete_->emplace_back(std::move(to_delete));
  ENVOY_LOG(trace, "item added to deferred deletion list (size={})", current_to_delete_->size());
  if (1 == current_to_delete_->size()) {
    deferred_delete_timer_->enableTimer(std::chrono::milliseconds(0));
  }
}

void DispatcherImpl::exit() { event_base_loopexit(base_.get(), nullptr); }

SignalEventPtr DispatcherImpl::listenForSignal(int signal_num, SignalCb cb) {
  ASSERT(isThreadSafe());
  return SignalEventPtr{new SignalEventImpl(*this, signal_num, cb)};
}

ChildProcessPtr DispatcherImpl::runProcess(const std::vector<std::string>& args,
                                           ProcessTerminationCb cb) {
  // Only register for SIGCHLD when this is used because only 1 libevent dispatcher may
  // register for signals.  Rely on the user of Dispatcher to know which one is used
  // for signals and to only call runProcess() on that one.
  if (!sigchld_) {
    sigchld_ = listenForSignal(SIGCHLD, [this]() -> void { child_manager_.onSigChld(); });
  }
  return child_manager_.run(args, std::move(cb));
}

void DispatcherImpl::post(std::function<void()> callback) {
  bool do_post;
  {
    std::unique_lock<std::mutex> lock(post_lock_);
    do_post = post_callbacks_.empty();
    post_callbacks_.push_back(callback);
  }

  if (do_post) {
    post_timer_->enableTimer(std::chrono::milliseconds(0));
  }
}

void DispatcherImpl::run(RunType type) {
  run_tid_ = Thread::Thread::currentThreadId();

  // Flush all post callbacks before we run the event loop. We do this because there are post
  // callbacks that have to get run before the initial event loop starts running. libevent does
  // not gaurantee that events are run in any particular order. So even if we post() and call
  // event_base_once() before some other event, the other event might get called first.
  runPostCallbacks();

  event_base_loop(base_.get(), type == RunType::NonBlock ? EVLOOP_NONBLOCK : 0);
}

void DispatcherImpl::runPostCallbacks() {
  std::unique_lock<std::mutex> lock(post_lock_);
  while (!post_callbacks_.empty()) {
    std::function<void()> callback = post_callbacks_.front();
    post_callbacks_.pop_front();

    lock.unlock();
    callback();
    lock.lock();
  }
}

ChildManager::ChildManager(OsSysCallsPtr syscalls) : syscalls_(std::move(syscalls)) {}

ChildProcessPtr ChildManager::run(const std::vector<std::string>& args, ProcessTerminationCb&& cb) {
  if (args.size() < 1 || args[0].empty()) {
    throw EnvoyException("Invalid arguments; must provide a command to run");
  }

  std::vector<const char*> argv;
  argv.reserve(args.size() + 1);

  for (const auto& arg : args) {
    argv.push_back(arg.c_str());
  }
  // execvp requires a null-terminated array of pointers
  argv.push_back(nullptr);

  const char* const process_path_ptr = argv[0];

  // C and C++ disagree on const rules and what can be cast implicitly.  In C
  // (where execvp is defined) this would be an automatic cast, and none of the
  // strings in argv will be modified.  In C++ we have to const_cast to make it
  // compile, or copy all the strings in the vector so we have a non-const
  // pointer to each.
  char* const* argv_ptr = const_cast<char* const*>(&argv[0]);

  pid_t pid = syscalls_->fork();
  if (pid == 0) {
    // This is the child

    // All other threads are destroyed upon fork(), so the process is in
    // an unknown state: mutexes may be locked, things may not be consistent,
    // etc.  Only functions that are safe to be called from signal handlers
    // may be called in this context.
    int dev_null = syscalls_->open("/dev/null", O_RDWR);
    syscalls_->dup2(dev_null, STDIN_FILENO);
    syscalls_->dup2(dev_null, STDOUT_FILENO);
    syscalls_->dup2(dev_null, STDERR_FILENO);
    syscalls_->close(dev_null);

    syscalls_->execvp(process_path_ptr, argv_ptr);

    // If this is reached, we failed to exec.  Exit immediately with non-zero status.
    syscalls_->_exit(1);

    // This path is only reached by tests
    return nullptr;
  } else if (pid > 0) {
    // This is the parent

    // Note: it is possible for the child to exit before any of this code runs,
    // but due to how libevent handles signals, the SIGCHLD handler won't run
    // until the dispatch loop runs again, so this should be safe from that race
    // condition.
    std::unique_ptr<ChildProcessImpl> child_process(new ChildProcessImpl(*this, pid));
    processes_[pid] = std::move(std::make_pair(std::move(cb), child_process.get()));
    return child_process;
  } else {
    ENVOY_LOG_MISC(warn, "Failed to fork before executing {}", args[0]);
    throw EnvoyException("Failed to fork");
  }
}

void ChildManager::onSigChld() {
  while (true) {
    OsSysCalls::WaitpidStatus status;
    pid_t pid = syscalls_->waitpid(-1, status, WNOHANG);
    if (pid <= 0) {
      break;
    }

    // We only care when a process exits (cleanly or uncleanly), not if
    // it was stopped/continued.
    if (status.exited_ || status.signalled_) {
      auto it = processes_.find(pid);
      if (it != processes_.end()) {
        it->second.first(status.exited_ && status.exit_status_ == 0);
        processes_.erase(it);
      } else {
        ENVOY_LOG_MISC(debug, "Child terminated that nobody was listening for: pid {}", pid);
      }
    }
  }
}

void ChildManager::remove(pid_t pid, ChildProcessImpl* child_process) {
  // If the pid isn't found, that means we already waited on the process and called
  // the completion callback.  Don't send it a signal in that case.
  //
  // If the completion pointer doesn't match, that means that a process was launched
  // for the PID, it completed, the ChildProcessImpl for it was never destructed, a
  // new process was launched with the same PID (PID recycle occurred), and now
  // we're destructing a stale ChildProcessImpl.  Don't signal the new process in this
  // case.
  auto it = processes_.find(pid);
  if (it != processes_.end() && it->second.second == child_process) {
    // Zero and negative are special and could signal many processes; ensure
    // we're doing what we intend and only signalling one process by using a positive
    // pid value.
    RELEASE_ASSERT(pid > 0);
    syscalls_->kill(pid, SIGTERM);
    processes_.erase(it);
  }
}

} // namespace Event
} // namespace Envoy
