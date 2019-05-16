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

private:
  friend class DnsResolverImplPeer;

  class AresWrapper {
  public:
    typedef std::function<void(void* arg, int status, int timeouts, hostent* hostent,
                               void* addrttls, int naddrttls)>
        AresHostCallback;

    /**
     * wrapper function of call to ares_gethostbyname.
     * @param channel ares_channel.
     * @param name host name.
     * @param family desired type of address for the resulting host entry.
     * @param callback when the query is complete or has failed, the ares library will invoke
     * callback.
     * @param arg argument passed to callback.
     */
    void GetHostbyName(ares_channel channel, const std::string& name, int family,
                       AresHostCallback callback, void* arg) const;

  private:
    struct HostQuery {
      HostQuery(ares_channel channel, const std::string& name, const int want_family,
                const int sent_family, AresHostCallback callback, void* arg, const char* lookups,
                const int timeouts)
          : channel_(channel), name_(name), callback_(callback), arg_(arg),
            sent_family_(sent_family), want_family_(want_family), remaining_lookups_(lookups),
            timeouts_(timeouts) {}

      ares_channel channel_;
      const std::string name_;
      AresHostCallback callback_;
      void* arg_;
      int sent_family_;
      int want_family_;
      const char* remaining_lookups_;
      int timeouts_;
    };

    // Lookup domain name.
    static void nextLookup(HostQuery* hquery, int statusCode);

    // Callback when the query is complete or has failed
    static void hostCallback(void* arg, int status, int timeouts, unsigned char* abuf, int alen);

    static void endHquery(HostQuery* hquery, int status, hostent* host, void* addrttls,
                          int naddrttls);

    // If the name looks like an IP address, fake up a host entry, end the query immediately, and
    // return true. Otherwise return false.
    static bool fakeHostent(const std::string& name, int family, AresHostCallback callback,
                            void* arg);

    // Check if domain is onion domain.
    static bool isOnionDomain(const std::string& name);

    // Maximum length of structure ares_addrttl (ares_addr6ttl for AF_INET6)
    static const int addrttl_len_ = 32;
  };

  struct PendingResolution : public ActiveDnsQuery {
    // Network::ActiveDnsQuery
    PendingResolution(ResolveCb callback, Event::Dispatcher& dispatcher, ares_channel channel,
                      const std::string& dns_name)
        : ares_wrapper_(), callback_(callback), dispatcher_(dispatcher), channel_(channel),
          dns_name_(dns_name) {}

    void cancel() override {
      // c-ares only supports channel-wide cancellation, so we just allow the
      // network events to continue but don't invoke the callback on completion.
      cancelled_ = true;
    }

    /**
     * AresWrapper GetHostbyName query callback.
     * @param status return status of call to GetHostbyName.
     * @param timeouts the number of times the request timed out.
     * @param hostent structure that stores information about a given host.
     * @addrttls structure that  stores ares_addrttl (ares_addr6ttl for AF_INET6).
     * @naddrttls number of ares_addrttl.
     */
    void onAresHostCallback(int status, int timeouts, hostent* hostent, void* addrttls,
                            int naddrttls);

    /**
     * wrapper function of call to GetHostbyName.
     * @param family currently AF_INET and AF_INET6 are supported.
     */
    void getHostByName(int family);

    const AresWrapper ares_wrapper_;
    // Caller supplied callback to invoke on query completion or error.
    const ResolveCb callback_;
    // Dispatcher to post any callback_ exceptions to.
    Event::Dispatcher& dispatcher_;
    // Does the object own itself? Resource reclamation occurs via self-deleting
    // on query completion or error.
    bool owned_ = false;
    // Has the query completed? Only meaningful if !owned_;
    bool completed_ = false;
    // Was the query cancelled via cancel()?
    bool cancelled_ = false;
    // If dns_lookup_family is "fallback", fallback to v4 address if v6
    // resolution failed.
    bool fallback_if_failed_ = false;
    const ares_channel channel_;
    const std::string dns_name_;
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

} // namespace Network
} // namespace Envoy
