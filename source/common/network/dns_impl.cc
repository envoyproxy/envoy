#include "dns_impl.h"

#include "common/common/assert.h"
#include "common/event/libevent.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "event2/event.h"

namespace Network {

DnsResolverImpl::DnsResolverImpl(Event::DispatcherImpl& dispatcher) : dispatcher_(dispatcher) {
  // This sets us up to receive signals on an fd when async DNS resolved are completed.
  sigset_t mask;
  sigemptyset(&mask);
  sigaddset(&mask, Event::Libevent::Global::DNS_SIGNAL_ID);
  signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK);
  RELEASE_ASSERT(-1 != signal_fd_);

  event_assign(&signal_read_event_, &dispatcher_.base(), signal_fd_,
               EV_READ | EV_PERSIST, [](evutil_socket_t, short, void* arg) -> void {
                 static_cast<DnsResolverImpl*>(arg)->onSignal();
               }, this);

  event_add(&signal_read_event_, nullptr);
}

DnsResolverImpl::~DnsResolverImpl() {
  close(signal_fd_);
  event_del(&signal_read_event_);
}

void DnsResolverImpl::onSignal() {
  while (true) {
    signalfd_siginfo signal_info;
    ssize_t rc = read(signal_fd_, &signal_info, sizeof(signal_info));
    if (rc == -1 && errno == EAGAIN) {
      break;
    }

    RELEASE_ASSERT(rc == sizeof(signal_info));
    PendingResolution* pending_resolution =
        reinterpret_cast<PendingResolution*>(signal_info.ssi_ptr);

    std::list<Address::InstancePtr> address_list;
    addrinfo* result = pending_resolution->async_cb_data_.ar_result;
    while (result != nullptr) {
      // TODO: IPv6 support.
      ASSERT(result->ai_family == AF_INET);
      sockaddr_in* address = reinterpret_cast<sockaddr_in*>(result->ai_addr);
      address_list.emplace_back(new Address::Ipv4Instance(address));
      result = result->ai_next;
    }

    freeaddrinfo(pending_resolution->async_cb_data_.ar_result);
    if (!pending_resolution->cancelled_) {
      // TODO: There is no good way to cancel a DNS request with the terrible getaddrinfo_a() API.
      //       We just mark it cancelled and ignore raising a callback. In the future when we switch
      //       this out for a better library we can actually cancel.
      pending_resolution->callback_(std::move(address_list));
    }
    pending_resolution->removeFromList(pending_resolutions_);
  }
}

ActiveDnsQuery& DnsResolverImpl::resolve(const std::string& dns_name, ResolveCb callback) {
  // This initializes the getaddrinfo_a callback data.
  PendingResolutionPtr pending_resolution(new PendingResolution());
  ActiveDnsQuery& ret = *pending_resolution;
  pending_resolution->host_ = dns_name;
  pending_resolution->async_cb_data_.ar_name = pending_resolution->host_.c_str();
  pending_resolution->async_cb_data_.ar_service = nullptr;
  pending_resolution->async_cb_data_.ar_request = &pending_resolution->hints_;
  pending_resolution->callback_ = callback;

  // This initializes the hints for the lookup.
  memset(&pending_resolution->hints_, 0, sizeof(pending_resolution->hints_));
  pending_resolution->hints_.ai_family = AF_INET;
  pending_resolution->hints_.ai_socktype = SOCK_STREAM;

  // This initializes the async signal data.
  sigevent signal_info;
  signal_info.sigev_notify = SIGEV_SIGNAL;
  signal_info.sigev_signo = Event::Libevent::Global::DNS_SIGNAL_ID;
  signal_info.sigev_value.sival_ptr = pending_resolution.get();

  gaicb* list[1];
  list[0] = &pending_resolution->async_cb_data_;
  pending_resolution->moveIntoList(std::move(pending_resolution), pending_resolutions_);
  int rc = getaddrinfo_a(GAI_NOWAIT, list, 1, &signal_info);
  RELEASE_ASSERT(0 == rc);
  UNREFERENCED_PARAMETER(rc);

  return ret;
}

} // Network
