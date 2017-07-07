#pragma once

#include <list>
#include <memory>
#include <string>

#include "envoy/event/dispatcher.h"
#include "envoy/stats/stats_macros.h"

#include "common/common/linked_object.h"

namespace Envoy {
namespace Network {

class ListenerImpl;

/**
 * All stats for the proxy protocol. @see stats_macros.h
 */
// clang-format off
#define ALL_PROXY_PROTOCOL_STATS(COUNTER)                                                          \
  COUNTER(downstream_cx_proxy_proto_error)
// clang-format on

/**
 * Definition of all stats for the proxy protocol. @see stats_macros.h
 */
struct ProxyProtocolStats {
  ALL_PROXY_PROTOCOL_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Implementation the PROXY Protocol V1
 * (http://www.haproxy.org/download/1.5/doc/proxy-protocol.txt)
 */
class ProxyProtocol {
public:
  class ActiveConnection : public LinkedObject<ActiveConnection> {
  public:
    ActiveConnection(ProxyProtocol& parent, Event::Dispatcher& dispatcher, int fd,
                     ListenerImpl& listener);
    ~ActiveConnection();

  private:
    static const size_t MAX_PROXY_PROTO_LEN = 108;

    void onRead();
    void onReadWorker();

    /**
     * Helper function that attempts to read a line (delimited by '\r\n') from the socket.
     * throws EnvoyException on any socket errors.
     * @return bool true if a line should be read, false if more data is needed.
     */
    bool readLine(int fd, std::string& s);
    void close();

    ProxyProtocol& parent_;
    int fd_;
    ListenerImpl& listener_;
    Event::FileEventPtr file_event_;

    // The offset in buf_ that has been fully read
    size_t buf_off_{};

    // The index in buf_ where the search for '\r\n' should continue from
    size_t search_index_;

    // Stores the portion of the first line that has been read so far.
    char buf_[MAX_PROXY_PROTO_LEN];
  };

  ProxyProtocol(Stats::Scope& scope);

  void newConnection(Event::Dispatcher& dispatcher, int fd, ListenerImpl& listener);

private:
  ProxyProtocolStats stats_;
  std::list<std::unique_ptr<ActiveConnection>> connections_;
};

} // namespace Network
} // namespace Envoy
