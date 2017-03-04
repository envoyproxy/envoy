#include "dns_impl.h"

#include "common/common/assert.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "ares.h"

namespace Network {

DnsResolverImpl::DnsResolverImpl(Event::Dispatcher& dispatcher)
    : dispatcher_(dispatcher),
      timer_(dispatcher.createTimer([this] { onEventCallback(ARES_SOCKET_BAD, 0); })) {
  // This is also done in main(), to satisfy the requirement that c-ares is
  // initialized prior to threading. The additional call to ares_library_init()
  // here is a nop in normal execution, but exists for testing where we don't
  // launch via main().
  ares_library_init(ARES_LIB_INIT_ALL);
  ares_options options;
  initializeChannel(&options, 0);
}

DnsResolverImpl::~DnsResolverImpl() {
  timer_->disableTimer();
  ares_destroy(channel_);
  ares_library_cleanup();
}

void DnsResolverImpl::initializeChannel(ares_options* options, int optmask) {
  options->sock_state_cb = [](void* arg, int fd, int read, int write) {
    static_cast<DnsResolverImpl*>(arg)->onAresSocketStateChange(fd, read, write);
  };
  options->sock_state_cb_data = this;
  ares_init_options(&channel_, options, optmask | ARES_OPT_SOCK_STATE_CB);
}

void DnsResolverImpl::PendingResolution::onAresHostCallback(int status, hostent* hostent) {
  // We receive ARES_EDESTRUCTION when destructing with pending queries.
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
      ASSERT(hostent->h_length == sizeof(in_addr));
      sockaddr_in address;
      memset(&address, 0, sizeof(address));
      // TODO(mattklein123): IPv6 support.
      address.sin_family = AF_INET;
      address.sin_port = 0;
      address.sin_addr = *reinterpret_cast<in_addr*>(hostent->h_addr_list[i]);
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

void DnsResolverImpl::updateAresTimer() {
  // Update the timeout for events.
  timeval timeout;
  timeval* timeout_result = ares_timeout(channel_, nullptr, &timeout);
  if (timeout_result != nullptr) {
    timer_->enableTimer(
        std::chrono::milliseconds(timeout_result->tv_sec * 1000 + timeout_result->tv_usec / 1000));
  } else {
    timer_->disableTimer();
  }
}

void DnsResolverImpl::onEventCallback(int fd, uint32_t events) {
  const ares_socket_t read_fd = events & Event::FileReadyType::Read ? fd : ARES_SOCKET_BAD;
  const ares_socket_t write_fd = events & Event::FileReadyType::Write ? fd : ARES_SOCKET_BAD;
  ares_process_fd(channel_, read_fd, write_fd);
  updateAresTimer();
}

void DnsResolverImpl::onAresSocketStateChange(int fd, int read, int write) {
  updateAresTimer();
  auto it = events_.find(fd);
  // Stop tracking events for fd if no more state change events.
  if (read == 0 && write == 0) {
    if (it != events_.end()) {
      events_.erase(it);
    }
    return;
  }

  // If we weren't tracking the fd before, create a new FileEvent.
  if (it == events_.end()) {
    events_[fd] = dispatcher_.createFileEvent(fd, [this, fd](uint32_t events) {
      onEventCallback(fd, events);
    }, Event::FileTriggerType::Level);
  }
  events_[fd]->setEnabled((read ? Event::FileReadyType::Read : 0) |
                          (write ? Event::FileReadyType::Write : 0));
}

ActiveDnsQuery* DnsResolverImpl::resolve(const std::string& dns_name, ResolveCb callback) {
  std::unique_ptr<PendingResolution> pending_resolution(new PendingResolution());
  pending_resolution->callback_ = callback;

  ares_gethostbyname(channel_, dns_name.c_str(),
                     AF_INET, [](void* arg, int status, int timeouts, hostent* hostent) {
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
