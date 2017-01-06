#pragma once

#include "envoy/network/dns.h"

#include "common/common/linked_object.h"
#include "common/event/dispatcher_impl.h"

#include "event2/event_struct.h"

namespace Network {

/**
 * Implementation of DnsResolver that uses getaddrinfo_a. All calls and callbacks are assumed to
 * happen on the thread that owns the creating dispatcher. Also, since results come in via signal
 * only one of these can exist at a time.
 */
class DnsResolverImpl : public DnsResolver {
public:
  DnsResolverImpl(Event::DispatcherImpl& dispatcher);
  ~DnsResolverImpl();

  // Network::DnsResolver
  ActiveDnsQuery& resolve(const std::string& dns_name, ResolveCb callback) override;

private:
  struct PendingResolution : LinkedObject<PendingResolution>, public ActiveDnsQuery {
    // Network::ActiveDnsQuery
    void cancel() override { cancelled_ = true; }

    std::string host_;
    addrinfo hints_;
    gaicb async_cb_data_;
    ResolveCb callback_;
    bool cancelled_{};
  };

  typedef std::unique_ptr<PendingResolution> PendingResolutionPtr;

  void onSignal();

  Event::DispatcherImpl& dispatcher_;
  int signal_fd_;
  event signal_read_event_;
  std::list<PendingResolutionPtr> pending_resolutions_;
};

} // Network
