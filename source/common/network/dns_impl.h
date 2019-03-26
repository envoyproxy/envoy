#pragma once

#include <netdb.h>

#include <cstdint>
#include <string>
#include <unordered_map>

#include "envoy/event/dispatcher.h"
#include "envoy/event/file_event.h"
#include "envoy/network/dns.h"

#include "common/common/linked_object.h"
#include "common/common/logger.h"
#include "common/common/utility.h"

#include "ares.h"

namespace Envoy {
namespace Network {

class DnsResolverImplPeer;

/**
 * Implementation of DnsResolver that uses c-ares. All calls and callbacks are assumed to
 * happen on the thread that owns the creating dispatcher.
 */
class DnsResolverImpl : public DnsResolver, protected Logger::Loggable<Logger::Id::upstream> {
public:
  DnsResolverImpl(Event::Dispatcher& dispatcher,
                  const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers);
  ~DnsResolverImpl() override;

  // Network::DnsResolver
  ActiveDnsQuery* resolve(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                          ResolveCb callback) override;
  ActiveDnsQuery* resolveSrv(const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                             ResolveSrvCb callback) override;

private:
  friend class DnsResolverImplPeer;
  struct PendingResolutionBase : public ActiveDnsQuery {
    // Network::ActiveDnsQuery
    PendingResolutionBase(Event::Dispatcher& dispatcher, ares_channel channel,
                          const std::string& dns_name)
        : dispatcher_(dispatcher), channel_(channel), dns_name_(dns_name) {}

    void cancel() override {
      // c-ares only supports channel-wide cancellation, so we just allow the
      // network events to continue but don't invoke the callback on completion.
      cancelled_ = true;
    }

    // Dispatcher to post any callback_ exceptions to.
    Event::Dispatcher& dispatcher_;
    // Does the object own itself? Resource reclamation occurs via self-deleting
    // on query completion or error.
    bool owned_ = false;
    // Has the query completed? Only meaningful if !owned_;
    bool completed_ = false;
    // Was the query cancelled via cancel()?
    bool cancelled_ = false;
    const ares_channel channel_;
    const std::string dns_name_;
  };

  struct PendingResolution : public PendingResolutionBase {
    PendingResolution(ResolveCb callback, Event::Dispatcher& dispatcher, ares_channel channel,
                      const std::string& dns_name)
        : PendingResolutionBase(dispatcher, channel, dns_name), callback_(callback) {}

    /**
     * ares_getaddrinfo query callback.
     * @param status return status of call to ares_getaddrinfo.
     * @param timeouts the number of times the request timed out.
     * @param addrinfo structure to store address info.
     */
    void onAresGetAddrInfoCallback(int status, int timeouts, ares_addrinfo* addrinfo);

    /**
     * wrapper function of call to ares_getaddrinfo.
     * @param family currently AF_INET and AF_INET6 are supported.
     */
    void getAddrInfo(int family);

    // Caller supplied callback to invoke on query completion or error.
    const ResolveCb callback_;
    // If dns_lookup_family is "fallback", fallback to v4 address if v6 resolution failed.
    bool fallback_if_failed_ = false;
  };

  struct PendingSrvResolution : public PendingResolutionBase {
    PendingSrvResolution(ResolveSrvCb callback, Event::Dispatcher& dispatcher, ares_channel channel,
                         const std::string& dns_name, DnsLookupFamily dns_lookup_family,
                         DnsResolverImpl* resolver)
        : PendingResolutionBase(dispatcher, channel, dns_name), callback_(callback),
          dns_lookup_family_(dns_lookup_family), resolver_(resolver) {}

    /**
     * c-ares ares_query() query callback for initiation.
     * @param status the status of the call to ares_query().
     * @param timeouts the number of times the request timed out.
     * @param buf the result buffer.
     * @param len the result buffer length.
     */
    void onAresSrvStartCallback(int status, int timeouts, unsigned char* buf, int len);

    /**
     * c-ares ares_query() query callback for completion.
     * @param srv_records a list of SRV records.
     */
    void onAresSrvFinishCallback(std::list<Address::SrvInstanceConstSharedPtr>&& srv_records);

    // wrapper function of call to ares_query().
    void getSrvByName();

    // Caller supplied callback to invoke on query completion or error.
    const ResolveSrvCb callback_;
    // The DnsLookupFamily for the SRV record.
    const DnsLookupFamily dns_lookup_family_;
    // The resolver instance.
    DnsResolverImpl* resolver_;
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
  /**
   * Marks a PendingResolutionBase object for destruction if owned.
   * @param a pointer to the PendingResolutionBase instance.
   */
  ActiveDnsQuery* markForDestruction(std::unique_ptr<PendingResolutionBase> pending_resolution);

  Event::Dispatcher& dispatcher_;
  Event::TimerPtr timer_;
  ares_channel channel_;
  std::unordered_map<int, Event::FileEventPtr> events_;
};

} // namespace Network
} // namespace Envoy
