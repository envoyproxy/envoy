#include "dns_impl.h"

#include "common/common/assert.h"
#include "common/event/libevent.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "ares.h"
#include "event2/event.h"

namespace Network {

DnsResolverImpl::DnsResolverImpl(Event::DispatcherImpl& dispatcher) : dispatcher_(dispatcher) {
  // This is also done in main(), to satisfy the requirement that c-ares is
  // initialized prior to threading. The additional call to ares_library_init()
  // here is a nop in normal execution, but exists for testing where we don't
  // launch via main().
  ares_library_init(ARES_LIB_INIT_ALL);
  struct ares_options options;
  initializeChannel(&options, 0);
}

DnsResolverImpl::~DnsResolverImpl() {
  ares_destroy(channel_);
  for (auto it : events_) {
    event_del(it.second);
    event_free(it.second);
  }
  events_.clear();
  ares_library_cleanup();
}

void DnsResolverImpl::initializeChannel(struct ares_options* options, int optmask) {
  options->sock_state_cb = [](void* arg, int fd, int read, int write) {
    static_cast<DnsResolverImpl*>(arg)->onAresSocketStateChange(fd, read, write);
  };
  options->sock_state_cb_data = this;
  ares_init_options(&channel_, options, optmask | ARES_OPT_SOCK_STATE_CB);
}

void DnsResolverImpl::PendingResolution::onAresHostCallback(int status, struct hostent* hostent) {
  if (status == ARES_EDESTRUCTION) {
    ASSERT(owned_);
    delete this;
    return;
  }
  std::list<Address::InstancePtr> address_list;
  completed_ = true;
  if (status == ARES_SUCCESS) {
    ASSERT(hostent->h_addrtype == AF_INET);
    for (int i = 0; hostent->h_addr_list[i] != nullptr; ++i) {
      ASSERT(hostent->h_length == sizeof(struct in_addr));
      struct sockaddr_in address;
      // TODO: IPv6 support.
      address.sin_family = AF_INET;
      address.sin_port = 0;
      address.sin_addr = *reinterpret_cast<struct in_addr*>(hostent->h_addr_list[i]);
      address_list.emplace_back(new Address::Ipv4Instance(&address));
    }
  }
  if (!cancelled_) {
    callback_(std::move(address_list));
  }
  if (owned_) {
    delete this;
  }
}

void DnsResolverImpl::onEventCallback(evutil_socket_t socket, short events, void* arg) {
  const ares_socket_t read_fd = events & EV_READ ? socket : ARES_SOCKET_BAD;
  const ares_socket_t write_fd = events & EV_WRITE ? socket : ARES_SOCKET_BAD;
  ares_process_fd(channel_, read_fd, write_fd);
  UNREFERENCED_PARAMETER(arg);
}

void DnsResolverImpl::onAresSocketStateChange(int fd, int read, int write) {
  auto it = events_.find(fd);
  // Stop tracking events for fd if no more state change events.
  if (read == 0 && write == 0) {
    if (it != events_.end()) {
      event_del(it->second);
      event_free(it->second);
      events_.erase(it);
    }
    return;
  }

  const short events = (read ? EV_READ : 0) | (write ? EV_WRITE : 0) | EV_PERSIST;
  auto on_event_callback_fn = [](evutil_socket_t socket, short events, void* arg) {
    static_cast<DnsResolverImpl*>(arg)->onEventCallback(socket, events, nullptr);
  };

  // Determine the timeout for events.
  struct timeval timeout;
  struct timeval* timeout_result = ares_timeout(channel_, nullptr, &timeout);

  // If we weren't tracking the fd before, create a new event and we're done.
  if (it == events_.end()) {
    event* ev = event_new(&dispatcher_.base(), fd, events, on_event_callback_fn, this);
    event_add(ev, timeout_result);
    events_[fd] = ev;
    return;
  }

  // Otherwise, need to re-assign the event for the fd.
  event_del(it->second);
  event_assign(it->second, &dispatcher_.base(), fd, events, on_event_callback_fn, this);
  event_add(it->second, timeout_result);
}

ActiveDnsQuery* DnsResolverImpl::resolve(const std::string& dns_name, ResolveCb callback) {
  std::unique_ptr<PendingResolution> pending_resolution(new PendingResolution());
  pending_resolution->callback_ = callback;

  ares_gethostbyname(channel_, dns_name.c_str(),
                     AF_INET, [](void* arg, int status, int timeouts, struct hostent* hostent) {
                       static_cast<PendingResolution*>(arg)->onAresHostCallback(status, hostent);
                       UNREFERENCED_PARAMETER(timeouts);
                     }, pending_resolution.get());

  if (pending_resolution->completed_) {
    return nullptr;
  } else {
    // The PendingResolution will self-delete when the request completes
    // (including if cancelled or if ~DnsResolverImpl() happens).
    pending_resolution->owned_ = true;
    return pending_resolution.release();
  }
}

} // Network
