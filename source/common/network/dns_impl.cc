#include "dns_impl.h"

#include "common/common/assert.h"
#include "common/event/libevent.h"
#include "common/network/utility.h"


namespace Network {

DnsResolverImpl::DnsResolverImpl(Event::DispatcherImpl& dispatcher) : dispatcher_(dispatcher) {}

DnsResolverImpl::~DnsResolverImpl() {
  pending_resolutions_.empty();
}

void DnsResolverImpl::onDNSResult(PendingResolution *resolution, int status, hostent* hostent) {
  if (status == ARES_EDESTRUCTION) {
    return;
  }

  std::list<std::string> address_list;
  if (status == ARES_SUCCESS && hostent->h_addr_list) {
    for (char** paddr = hostent->h_addr_list; *paddr != NULL; paddr++) {
      ASSERT(resolution->hints_.ai_family == hostent->h_addrtype);

      sockaddr_in address;
      address.sin_addr = *reinterpret_cast<in_addr*>(*paddr);
      address_list.emplace_back(Network::Utility::getAddressName(&address));
    }
  }

  if (status != ARES_ECANCELLED) {
    resolution->callback_(std::move(address_list));
  }

  resolution->resolved = true;

  auto removed = resolution->removeFromList(pending_resolutions_);
  dispatcher_.deferredDelete(std::move(removed));
}

void DnsResolverImpl::dispatchEvent(PendingResolution *pending_resolution) {
  int fd;
  timeval tv;

  if (ares_getsock(pending_resolution->channel_, &fd, 1) == 0) {
    return;
  }

  event_assign(
    &pending_resolution->fd_event_, &dispatcher_.base(), fd, EV_READ | EV_TIMEOUT,
    [](int fd, short event, void *arg) -> void {
      if (event & EV_READ) {
        auto resolution = reinterpret_cast<PendingResolution *>(arg);
        ares_process_fd(resolution->channel_, fd, ARES_SOCKET_BAD);
      }
    },
    static_cast<void *>(pending_resolution));

  auto tvp = ares_timeout(pending_resolution->channel_, NULL, &tv);
  event_add(&pending_resolution->fd_event_, tvp);
}

DnsResolverImpl::PendingResolutionPtr DnsResolverImpl::makePendingResolution(ResolveCb callback) {
  PendingResolutionPtr pending_resolution(new PendingResolution());
  pending_resolution->hints_.ai_family = AF_INET;
  pending_resolution->hints_.ai_socktype = SOCK_STREAM;
  pending_resolution->callback_ = callback;

  static char lookups[] = "fb";
  ares_options opts;
  opts.lookups = lookups;

  int rc = ares_init_options(&pending_resolution->channel_, &opts, ARES_OPT_LOOKUPS);
  RELEASE_ASSERT(rc == ARES_SUCCESS);

  return pending_resolution;
}

ActiveDnsQuery& DnsResolverImpl::resolve(const std::string& dns_name, ResolveCb callback) {
  auto pending_resolution_ptr = makePendingResolution(callback);
  auto pending_resolution = pending_resolution_ptr.get();
  auto pair = new ResolverPendingPair(this, pending_resolution);

  pending_resolution_ptr->moveIntoList(std::move(pending_resolution_ptr), pending_resolutions_);

  ares_gethostbyname(
   pending_resolution->channel_, dns_name.c_str(), pending_resolution->hints_.ai_family,
   [](void* arg, int status, int, hostent* hostent) -> void {
     auto pair = reinterpret_cast<ResolverPendingPair*>(arg);
     pair->first->onDNSResult(pair->second, status, hostent);
     delete pair;
   },
   static_cast<void*>(pair));

  if (!pending_resolution->resolved) {
    dispatchEvent(pending_resolution);
  }

  return *pending_resolution;
}

} // Network
