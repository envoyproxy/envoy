#pragma once

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/network/dns.h"

#include "common/common/linked_object.h"

#include "ares.h"

namespace Network {

class DnsResolverImplPeer;

/**
 * Implementation of DnsResolver that uses c-ares. All calls and callbacks are assumed to
 * happen on the thread that owns the creating dispatcher.
 */
class DnsResolverImpl : public DnsResolver {
public:
  DnsResolverImpl(Event::Dispatcher& dispatcher);
  ~DnsResolverImpl() override;

  // Network::DnsResolver
  ActiveDnsQuery* resolve(const std::string& dns_name, uint32_t port, ResolveCb callback) override;

private:
  friend class DnsResolverImplPeer;
  struct SrvQueryTask;
  struct PendingResolution : public ActiveDnsQuery {
    explicit PendingResolution(DnsResolverImpl* resolver, uint32_t port)
        : resolver_(resolver), port_(port) {}

    // Network::ActiveDnsQuery
    void cancel() override {
      // c-ares only supports channel-wide cancellation, so we just allow the
      // network events to continue but don't invoke the callback on completion.
      cancelled_ = true;
    }

    // c-ares ares_gethostbyname() query callback.
    void onAresHostCallback(int status, hostent* hostent);
    // c-areas ares_query SRV query callback.
    void onAresSrvStartCallback(int status, unsigned char* buf, int len);
    void onAresSrvFinishCallback(std::list<Address::InstancePtr>&& address_list);

    // The resolver instance
    DnsResolverImpl* resolver_;
    // Port of the result address, 0 for SRV queries
    uint32_t port_;
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

  // Callback for events on sockets tracked in events_.
  void onEventCallback(int fd, uint32_t events);
  // c-ares callback when a socket state changes, indicating that libevent
  // should listen for read/write events.
  void onAresSocketStateChange(int fd, int read, int write);
  // Initialize the channel with given ares_init_options().
  void initializeChannel(ares_options* options, int optmask);
  // Update timer for c-ares timeouts.
  void updateAresTimer();

  Event::Dispatcher& dispatcher_;
  Event::TimerPtr timer_;
  ares_channel channel_;
  std::unordered_map<int, Event::FileEventPtr> events_;
};

} // Network
