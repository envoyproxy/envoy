#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/scope_tracker.h"
#include "envoy/common/time.h"
#include "envoy/event/file_event.h"
#include "envoy/event/signal.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/watcher.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/dns.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/thread/thread.h"

namespace Envoy {
namespace Event {

/**
 * All dispatcher stats. @see stats_macros.h
 */
// clang-format off
#define ALL_DISPATCHER_STATS(HISTOGRAM)                                                            \
  HISTOGRAM(loop_duration_us, Microseconds)                                                        \
  HISTOGRAM(poll_delay_us, Microseconds)
// clang-format on

/**
 * Struct definition for all dispatcher stats. @see stats_macros.h
 */
struct DispatcherStats {
  ALL_DISPATCHER_STATS(GENERATE_HISTOGRAM_STRUCT)
};

/**
 * Callback invoked when a dispatcher post() runs.
 */
using PostCb = std::function<void()>;

/**
 * Abstract event dispatching loop.
 */
class Dispatcher {
public:
  virtual ~Dispatcher() = default;

  /**
   * Returns a time-source to use with this dispatcher.
   */
  virtual TimeSource& timeSource() PURE;

  /**
   * Initializes stats for this dispatcher. Note that this can't generally be done at construction
   * time, since the main and worker thread dispatchers are constructed before
   * ThreadLocalStoreImpl::initializeThreading.
   * @param scope the scope to contain the new per-dispatcher stats created here.
   * @param prefix the stats prefix to identify this dispatcher.
   */
  virtual void initializeStats(Stats::Scope& scope, const std::string& prefix) PURE;

  /**
   * Clears any items in the deferred deletion queue.
   */
  virtual void clearDeferredDeleteList() PURE;

  /**
   * Wraps an already-accepted socket in an instance of Envoy's server Network::Connection.
   * @param socket supplies an open file descriptor and connection metadata to use for the
   *        connection. Takes ownership of the socket.
   * @param transport_socket supplies a transport socket to be used by the connection.
   * @return Network::ConnectionPtr a server connection that is owned by the caller.
   */
  virtual Network::ConnectionPtr
  createServerConnection(Network::ConnectionSocketPtr&& socket,
                         Network::TransportSocketPtr&& transport_socket) PURE;

  /**
   * Creates an instance of Envoy's Network::ClientConnection. Does NOT initiate the connection;
   * the caller must then call connect() on the returned Network::ClientConnection.
   * @param address supplies the address to connect to.
   * @param source_address supplies an address to bind to or nullptr if no bind is necessary.
   * @param transport_socket supplies a transport socket to be used by the connection.
   * @param options the socket options to be set on the underlying socket before anything is sent
   *        on the socket.
   * @return Network::ClientConnectionPtr a client connection that is owned by the caller.
   */
  virtual Network::ClientConnectionPtr
  createClientConnection(Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address,
                         Network::TransportSocketPtr&& transport_socket,
                         const Network::ConnectionSocket::OptionsSharedPtr& options) PURE;

  /**
   * Creates an async DNS resolver. The resolver should only be used on the thread that runs this
   * dispatcher.
   * @param resolvers supplies the addresses of DNS resolvers that this resolver should use. If left
   * empty, it will not use any specific resolvers, but use defaults (/etc/resolv.conf)
   * @return Network::DnsResolverSharedPtr that is owned by the caller.
   */
  virtual Network::DnsResolverSharedPtr
  createDnsResolver(const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers) PURE;

  /**
   * Creates a file event that will signal when a file is readable or writable. On UNIX systems this
   * can be used for any file like interface (files, sockets, etc.).
   * @param fd supplies the fd to watch.
   * @param cb supplies the callback to fire when the file is ready.
   * @param trigger specifies whether to edge or level trigger.
   * @param events supplies a logical OR of FileReadyType events that the file event should
   *               initially listen on.
   */
  virtual FileEventPtr createFileEvent(int fd, FileReadyCb cb, FileTriggerType trigger,
                                       uint32_t events) PURE;

  /**
   * @return Filesystem::WatcherPtr a filesystem watcher owned by the caller.
   */
  virtual Filesystem::WatcherPtr createFilesystemWatcher() PURE;

  /**
   * Creates a listener on a specific port.
   * @param socket supplies the socket to listen on.
   * @param cb supplies the callbacks to invoke for listener events.
   * @param bind_to_port controls whether the listener binds to a transport port or not.
   * @return Network::ListenerPtr a new listener that is owned by the caller.
   */
  virtual Network::ListenerPtr
  createListener(Network::Socket& socket, Network::ListenerCallbacks& cb, bool bind_to_port) PURE;

  /**
   * Creates a logical udp listener on a specific port.
   * @param socket supplies the socket to listen on.
   * @param cb supplies the udp listener callbacks to invoke for listener events.
   * @return Network::ListenerPtr a new listener that is owned by the caller.
   */
  virtual Network::UdpListenerPtr createUdpListener(Network::Socket& socket,
                                                    Network::UdpListenerCallbacks& cb) PURE;
  /**
   * Allocates a timer. @see Timer for docs on how to use the timer.
   * @param cb supplies the callback to invoke when the timer fires.
   */
  virtual Event::TimerPtr createTimer(TimerCb cb) PURE;

  /**
   * Submits an item for deferred delete. @see DeferredDeletable.
   */
  virtual void deferredDelete(DeferredDeletablePtr&& to_delete) PURE;

  /**
   * Exits the event loop.
   */
  virtual void exit() PURE;

  /**
   * Listens for a signal event. Only a single dispatcher in the process can listen for signals.
   * If more than one dispatcher calls this routine in the process the behavior is undefined.
   *
   * @param signal_num supplies the signal to listen on.
   * @param cb supplies the callback to invoke when the signal fires.
   * @return SignalEventPtr a signal event that is owned by the caller.
   */
  virtual SignalEventPtr listenForSignal(int signal_num, SignalCb cb) PURE;

  /**
   * Posts a functor to the dispatcher. This is safe cross thread. The functor runs in the context
   * of the dispatcher event loop which may be on a different thread than the caller.
   */
  virtual void post(PostCb callback) PURE;

  /**
   * Runs the event loop. This will not return until exit() is called either from within a callback
   * or from a different thread.
   * @param type specifies whether to run in blocking mode (run() will not return until exit() is
   *              called) or non-blocking mode where only active events will be executed and then
   *              run() will return.
   */
  enum class RunType {
    Block,       // Executes any events that have been activated, then exit.
    NonBlock,    // Waits for any pending events to activate, executes them,
                 // then exits. Exits immediately if there are no pending or
                 // active events.
    RunUntilExit // Runs the event-loop until loopExit() is called, blocking
                 // until there are pending or active events.
  };
  virtual void run(RunType type) PURE;

  /**
   * Returns a factory which connections may use for watermark buffer creation.
   * @return the watermark buffer factory for this dispatcher.
   */
  virtual Buffer::WatermarkFactory& getWatermarkFactory() PURE;

  /**
   * Sets a tracked object, which is currently operating in this Dispatcher.
   * This should be cleared with another call to setTrackedObject() when the object is done doing
   * work. Calling setTrackedObject(nullptr) results in no object being tracked.
   *
   * This is optimized for performance, to avoid allocation where we do scoped object tracking.
   *
   * @return The previously tracked object or nullptr if there was none.
   */
  virtual const ScopeTrackedObject* setTrackedObject(const ScopeTrackedObject* object) PURE;

  /**
   * Validates that an operation is thread-safe with respect to this dispatcher; i.e. that the
   * current thread of execution is on the same thread upon which the dispatcher loop is running.
   */
  virtual bool isThreadSafe() const PURE;

  /**
   * Returns a recently cached MonotonicTime value.
   */
  virtual MonotonicTime approximateMonotonicTime() const PURE;

  /**
   * Updates approximate monotonic time to current value.
   */
  virtual void updateApproximateMonotonicTime() PURE;
};

using DispatcherPtr = std::unique_ptr<Dispatcher>;

} // namespace Event
} // namespace Envoy
