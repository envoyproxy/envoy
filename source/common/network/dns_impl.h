#pragma once

#include "envoy/network/dns.h"

#include "common/common/linked_object.h"
#include "common/event/dispatcher_impl.h"

#include "ares.h"
#include "event2/event_struct.h"

namespace Network {

class DnsResolverImplPeer;

/**
 * Implementation of DnsResolver that uses c-ares. All calls and callbacks are assumed to
 * happen on the thread that owns the creating dispatcher.
 */
class DnsResolverImpl : public DnsResolver {
public:
  DnsResolverImpl(Event::DispatcherImpl& dispatcher);
  ~DnsResolverImpl() override;

  // Network::DnsResolver
  ActiveDnsQuery* resolve(const std::string& dns_name, ResolveCb callback) override;

private:
  friend class DnsResolverImplPeer;
  struct PendingResolution : public ActiveDnsQuery {
    // Network::ActiveDnsQuery
    void cancel() override {
      // c-ares only supports channel-wide cancellation, so we just allow the
      // network events to continue but don't invoke the callback on completion.
      cancelled_ = true;
    }

    // c-ares ares_gethostbyname() query callback.
    void onAresHostCallback(int status, struct hostent* hostent);

    // Caller supplied callback to invoke on query completion or error.
    ResolveCb callback_;
    // Does the object own itself? Resource reclamation occurs via self-deleting
    // on query completion or error.
    bool owned_ = false;
    // Has the query completed? Only meaningful if !owned_;
    bool completed_ = false;
    // Was the query cancelled via cancel()?
    bool cancelled_ = false;
  };

  // libevent callback for events on sockets tracked in events_.
  void onEventCallback(evutil_socket_t socket, short events, void* arg);
  // c-ares callback when a socket state changes, indicating that libevent
  // should listen for read/write events.
  void onAresSocketStateChange(int fd, int read, int write);
  // Initialize the channel with given ares_init_options().
  void initializeChannel(struct ares_options* options, int optmask);

  Event::DispatcherImpl& dispatcher_;
  ares_channel channel_;
  std::unordered_map<int, event*> events_;
};

} // Network
