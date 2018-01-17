#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "envoy/event/file_event.h"
#include "envoy/event/signal.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/network/connection.h"
#include "envoy/network/connection_handler.h"
#include "envoy/network/dns.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/listener.h"
#include "envoy/network/transport_socket.h"
#include "envoy/stats/stats.h"

namespace Envoy {
namespace Event {

/**
 * Callback invoked when a dispatcher post() runs.
 */
typedef std::function<void()> PostCb;

/**
 * Abstract event dispatching loop.
 */
class Dispatcher {
public:
  virtual ~Dispatcher() {}

  /**
   * Clear any items in the deferred deletion queue.
   */
  virtual void clearDeferredDeleteList() PURE;

  /**
   * Create a client connection.
   * @param address supplies the address to connect to.
   * @param source_address supplies an address to bind to or nullptr if no bind is necessary.
   * @return Network::ClientConnectionPtr a client connection that is owned by the caller.
   */
  virtual Network::ClientConnectionPtr
  createClientConnection(Network::Address::InstanceConstSharedPtr address,
                         Network::Address::InstanceConstSharedPtr source_address,
                         Network::TransportSocketPtr&& transport_socket) PURE;

  /**
   * Create an async DNS resolver. The resolver should only be used on the thread that runs this
   * dispatcher.
   * @param resolvers supplies the addresses of DNS resolvers that this resolver should use. If left
   * empty, it will not use any specific resolvers, but use defaults (/etc/resolv.conf)
   * @return Network::DnsResolverSharedPtr that is owned by the caller.
   */
  virtual Network::DnsResolverSharedPtr
  createDnsResolver(const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers) PURE;

  /**
   * Create a file event that will signal when a file is readable or writable. On UNIX systems this
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
   * Create a listener on a specific port.
   * @param conn_handler supplies the handler for connections received by the listener
   * @param socket supplies the socket to listen on.
   * @param cb supplies the callbacks to invoke for listener events.
   * @param scope supplies the Stats::Scope to use.
   * @param listener_options listener configuration options.
   * @return Network::ListenerPtr a new listener that is owned by the caller.
   */
  virtual Network::ListenerPtr
  createListener(Network::ConnectionHandler& conn_handler, Network::ListenSocket& socket,
                 Network::ListenerCallbacks& cb, Stats::Scope& scope,
                 const Network::ListenerOptions& listener_options) PURE;

  /**
   * Create a listener on a specific port.
   * @param conn_handler supplies the handler for connections received by the listener
   * @param ssl_ctx supplies the SSL context to use.
   * @param socket supplies the socket to listen on.
   * @param cb supplies the callbacks to invoke for listener events.
   * @param scope supplies the Stats::Scope to use.
   * @param listener_options listener configuration options.
   * @return Network::ListenerPtr a new listener that is owned by the caller.
   */
  virtual Network::ListenerPtr
  createSslListener(Network::ConnectionHandler& conn_handler, Ssl::ServerContext& ssl_ctx,
                    Network::ListenSocket& socket, Network::ListenerCallbacks& cb,
                    Stats::Scope& scope, const Network::ListenerOptions& listener_options) PURE;

  /**
   * Allocate a timer. @see Event::Timer for docs on how to use the timer.
   * @param cb supplies the callback to invoke when the timer fires.
   */
  virtual TimerPtr createTimer(TimerCb cb) PURE;

  /**
   * Submit an item for deferred delete. @see DeferredDeletable.
   */
  virtual void deferredDelete(DeferredDeletablePtr&& to_delete) PURE;

  /**
   * Exit the event loop.
   */
  virtual void exit() PURE;

  /**
   * Listen for a signal event. Only a single dispatcher in the process can listen for signals.
   * If more than one dispatcher calls this routine in the process the behavior is undefined.
   *
   * @param signal_num supplies the signal to listen on.
   * @param cb supplies the callback to invoke when the signal fires.
   * @return SignalEventPtr a signal event that is owned by the caller.
   */
  virtual SignalEventPtr listenForSignal(int signal_num, SignalCb cb) PURE;

  /**
   * Post a functor to the dispatcher. This is safe cross thread. The functor runs in the context
   * of the dispatcher event loop which may be on a different thread than the caller.
   */
  virtual void post(PostCb callback) PURE;

  /**
   * Run the event loop. This will not return until exit() is called either from within a callback
   * or from a different thread.
   * @param type specifies whether to run in blocking mode (run() will not return until exit() is
   *              called) or non-blocking mode where only active events will be executed and then
   *              run() will return.
   */
  enum class RunType { Block, NonBlock };
  virtual void run(RunType type) PURE;

  /**
   * Returns a factory which connections may use for watermark buffer creation.
   * @return the watermark buffer factory for this dispatcher.
   */
  virtual Buffer::WatermarkFactory& getWatermarkFactory() PURE;
};

typedef std::unique_ptr<Dispatcher> DispatcherPtr;

} // namespace Event
} // namespace Envoy
