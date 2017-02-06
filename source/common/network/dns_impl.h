#pragma once

#include "ares.h"

#include "envoy/network/dns.h"
#include "envoy/event/deferred_deletable.h"

#include "common/common/linked_object.h"
#include "common/event/dispatcher_impl.h"

#include "event2/event_struct.h"
#include "event2/event.h"

namespace Network {

/**
 * Implementation of DnsResolver that uses c-ares. All calls and callbacks are assumed to
 * happen on the thread that owns the creating dispatcher.
 */
class DnsResolverImpl : public DnsResolver {
public:
  DnsResolverImpl(Event::DispatcherImpl& dispatcher);
  ~DnsResolverImpl();

  // Network::DnsResolver
  ActiveDnsQuery& resolve(const std::string& dns_name, ResolveCb callback) override;

private:
  struct PendingResolution : LinkedObject<PendingResolution>,
                             public ActiveDnsQuery,
                             public Event::DeferredDeletable {
    ~PendingResolution() {
      if (channel_) {
        ares_destroy(channel_);
        channel_ = nullptr;
      }

      // Some resolutions don't need an event (e.g. localhost) and are resolved synchronously
      if (NULL != fd_event_.ev_base) {
        event_del(&fd_event_);
      }
    }

    // Network::ActiveDnsQuery
    void cancel() override { ares_cancel(channel_); }

    ResolveCb callback_;
    ares_channel channel_;
    event fd_event_;
    addrinfo hints_;
    bool resolved;
  };

  typedef std::unique_ptr<PendingResolution> PendingResolutionPtr;
  typedef std::pair<DnsResolverImpl*, PendingResolution*> ResolverPendingPair;

  // Network::DnsResolverImpl
  void dispatchEvent(PendingResolution *pending_resolution);
  PendingResolutionPtr makePendingResolution(ResolveCb callback);
  void onDNSResult(PendingResolution *resolution, int status, hostent* hostent);

  Event::DispatcherImpl& dispatcher_;
  std::list<PendingResolutionPtr> pending_resolutions_;
};

} // Network
