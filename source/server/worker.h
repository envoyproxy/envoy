#pragma once

#include "connection_handler.h"

#include "envoy/server/configuration.h"
#include "envoy/thread_local/thread_local.h"

#include "common/common/thread.h"
#include "common/network/listen_socket_impl.h"

typedef std::map<Server::Configuration::Listener*, Network::TcpListenSocketPtr> SocketMap;

/**
 * A server threaded worker that wraps up a worker thread, event loop, etc.
 */
class Worker : Logger::Loggable<Logger::Id::main> {
public:
  Worker(Stats::Store& stats_store, ThreadLocal::Instance& tls);
  ~Worker();

  Event::Dispatcher& dispatcher() { return handler_->dispatcher(); }
  ConnectionHandler* handler() { return handler_.get(); }
  void initializeConfiguration(Server::Configuration::Main& config, const SocketMap& socket_map);

  /**
   * Exit the worker. Will block until the worker thread joins. Called from the main thread.
   */
  void exit();

private:
  void onNoExitTimer();
  void threadRoutine();

  ThreadLocal::Instance& tls_;
  std::unique_ptr<ConnectionHandler> handler_;
  Event::TimerPtr no_exit_timer_;
  Thread::ThreadPtr thread_;
};

typedef std::unique_ptr<Worker> WorkerPtr;
