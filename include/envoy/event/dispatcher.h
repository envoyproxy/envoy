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
   * Returns a time-source to use with this dispatcher.
   *
   * TODO(#4160) the implementations currently manage timer events that
   * ignore the time-source, and thus can't be mocked or faked. So it's
   * difficult to mock time in an integration test without mocking out
   * the dispatcher.
   */
  virtual TimeSource& timeSource() PURE;

  /**
   * Clear any items in the deferred deletion queue.
   */
  virtual void clearDeferredDeleteList() PURE;

  /**
   * Create a server connection.
   * @param socket supplies an open file descriptor and connection metadata to use for the
   *        connection. Takes ownership of the socket.
   * @param transport_socket supplies a transport socket to be used by the connection.
   * @return Network::ConnectionPtr a server connection that is owned by the caller.
   */
  virtual Network::ConnectionPtr
  createServerConnection(Network::ConnectionSocketPtr&& socket,
                         Network::TransportSocketPtr&& transport_socket) PURE;

  /**
   * Create a client connection.
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
   * @param socket supplies the socket to listen on.
   * @param cb supplies the callbacks to invoke for listener events.
   * @param bind_to_port controls whether the listener binds to a transport port or not.
   * @param hand_off_restored_destination_connections controls whether the listener searches for
   *        another listener after restoring the destination address of a new connection.
   * @return Network::ListenerPtr a new listener that is owned by the caller.
   */
  virtual Network::ListenerPtr createListener(Network::Socket& socket,
                                              Network::ListenerCallbacks& cb, bool bind_to_port,
                                              bool hand_off_restored_destination_connections) PURE;

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
