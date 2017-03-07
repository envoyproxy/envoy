#pragma once

#include "envoy/event/file_event.h"
#include "envoy/event/signal.h"
#include "envoy/event/timer.h"
#include "envoy/filesystem/filesystem.h"
#include "envoy/network/connection.h"
#include "envoy/network/dns.h"
#include "envoy/network/listener.h"
#include "envoy/network/listen_socket.h"
#include "envoy/network/connection_handler.h"
#include "envoy/ssl/context.h"
#include "envoy/stats/stats.h"

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
   * @return Network::ClientConnectionPtr a client connection that is owned by the caller.
   */
  virtual Network::ClientConnectionPtr
  createClientConnection(Network::Address::InstancePtr address) PURE;

  /**
   * Create an SSL client connection.
   * @param ssl_ctx supplies the SSL context to use.
   * @param address supplies the address to connect to.
   * @return Network::ClientConnectionPtr a client connection that is owned by the caller.
   */
  virtual Network::ClientConnectionPtr
  createSslClientConnection(Ssl::ClientContext& ssl_ctx,
                            Network::Address::InstancePtr address) PURE;

  /**
   * Create an async DNS resolver. Only a single resolver can exist in the process at a time and it
   * should only be used on the thread that runs this dispatcher.
   * @return Network::DnsResolverPtr that is owned by the caller.
   */
  virtual Network::DnsResolverPtr createDnsResolver() PURE;

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
   * @param stats_store supplies the Stats::Store to use.
   * @param bind_to_port specifies if the listener should actually bind to the port.
   *        a listener that doesn't bind can only receive connections redirected from
   *        other listeners that that set use_origin_dst to true
   * @param use_proxy_proto whether to use the PROXY Protocol V1
   * (http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt)
   * @param use_orig_dst if a connection was redirected to this port using iptables,
   *        allow the listener to hand it off to the listener associated to the original port
   * @param per_connection_buffer_limit_bytes soft limit on size of the listener's new connection
   *        read and write buffers.
   * @return Network::ListenerPtr a new listener that is owned by the caller.
   */
  virtual Network::ListenerPtr createListener(Network::ConnectionHandler& conn_handler,
                                              Network::ListenSocket& socket,
                                              Network::ListenerCallbacks& cb,
                                              Stats::Store& stats_store, bool bind_to_port,
                                              bool use_proxy_proto, bool use_orig_dst,
                                              size_t per_connection_buffer_limit_bytes) PURE;

  /**
   * Create a listener on a specific port.
   * @param conn_handler supplies the handler for connections received by the listener
   * @param ssl_ctx supplies the SSL context to use.
   * @param socket supplies the socket to listen on.
   * @param cb supplies the callbacks to invoke for listener events.
   * @param stats_store supplies the Stats::Store to use.
   * @param bind_to_port specifies if the listener should actually bind to the port.
   *        a listener that doesn't bind can only receive connections redirected from
   *        other listeners that set use_origin_dst to true
   * @param use_proxy_proto whether to use the PROXY Protocol V1
   * (http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt)
   * @param use_orig_dst if a connection was redirected to this port using iptables,
   *        allow the listener to hand it off to the listener associated to the original port
   * @param per_connection_buffer_limit_bytes soft limit on size of the listener's new connection
   *        read and write buffers.
   * @return Network::ListenerPtr a new listener that is owned by the caller.
   */
  virtual Network::ListenerPtr
  createSslListener(Network::ConnectionHandler& conn_handler, Ssl::ServerContext& ssl_ctx,
                    Network::ListenSocket& socket, Network::ListenerCallbacks& cb,
                    Stats::Store& stats_store, bool bind_to_port, bool use_proxy_proto,
                    bool use_orig_dst, size_t per_connection_buffer_limit_bytes) PURE;

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
};

typedef std::unique_ptr<Dispatcher> DispatcherPtr;

} // Event
