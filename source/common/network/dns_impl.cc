#include "common/network/dns_impl.h"

#include <arpa/nameser.h>
#include <arpa/nameser_compat.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "ares.h"

namespace Envoy {
namespace Network {

DnsResolverImpl::DnsResolverImpl(
    Event::Dispatcher& dispatcher,
    const std::vector<Network::Address::InstanceConstSharedPtr>& resolvers)
    : dispatcher_(dispatcher),
      timer_(dispatcher.createTimer([this] { onEventCallback(ARES_SOCKET_BAD, 0); })) {
  ares_options options;

  initializeChannel(&options, 0);

  if (!resolvers.empty()) {
    std::vector<std::string> resolver_addrs;
    resolver_addrs.reserve(resolvers.size());
    for (const auto& resolver : resolvers) {
      // This should be an IP address (i.e. not a pipe).
      if (resolver->ip() == nullptr) {
        ares_destroy(channel_);
        throw EnvoyException(
            fmt::format("DNS resolver '{}' is not an IP address", resolver->asString()));
      }
      // Note that the ip()->port() may be zero if the port is not fully specified by the
      // Address::Instance.
      // resolver->asString() is avoided as that format may be modified by custom
      // Address::Instance implementations in ways that make the <port> not a simple
      // integer. See https://github.com/envoyproxy/envoy/pull/3366.
      resolver_addrs.push_back(fmt::format(resolver->ip()->ipv6() ? "[{}]:{}" : "{}:{}",
                                           resolver->ip()->addressAsString(),
                                           resolver->ip()->port()));
    }
    const std::string resolvers_csv = StringUtil::join(resolver_addrs, ",");
    int result = ares_set_servers_ports_csv(channel_, resolvers_csv.c_str());
    RELEASE_ASSERT(result == ARES_SUCCESS, "");
  }
}

DnsResolverImpl::~DnsResolverImpl() {
  timer_->disableTimer();
  ares_destroy(channel_);
}

void DnsResolverImpl::initializeChannel(ares_options* options, int optmask) {
  options->sock_state_cb = [](void* arg, int fd, int read, int write) {
    static_cast<DnsResolverImpl*>(arg)->onAresSocketStateChange(fd, read, write);
  };
  options->sock_state_cb_data = this;
  ares_init_options(&channel_, options, optmask | ARES_OPT_SOCK_STATE_CB);
}

void DnsResolverImpl::PendingResolution::onAresHostCallback(int status, int timeouts,
                                                            hostent* hostent, void* addrttls,
                                                            int naddrttls) {
  // We receive ARES_EDESTRUCTION when destructing with pending queries.
  if (status == ARES_EDESTRUCTION) {
    ASSERT(owned_);
    delete this;
    return;
  }
  if (!fallback_if_failed_) {
    completed_ = true;
  }

  std::list<Address::InstanceConstSharedPtr> address_list;
  if (status == ARES_SUCCESS) {
    if (hostent->h_addrtype == AF_INET) {
      auto addrttls_ = static_cast<ares_addrttl*>(addrttls);
      std::unordered_map<std::string, std::chrono::seconds> ttl;

      for (int i = 0; i < naddrttls; ++i) {
        char buffer[INET_ADDRSTRLEN];
        ares_inet_ntop(AF_INET, &(addrttls_[i].ipaddr), buffer, INET_ADDRSTRLEN);
        ttl[std::string(buffer)] = std::chrono::seconds(addrttls_[i].ttl);
      }

      for (int i = 0; hostent->h_addr_list[i] != nullptr; ++i) {
        ASSERT(hostent->h_length == sizeof(in_addr));
        sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = 0;
        address.sin_addr = *reinterpret_cast<in_addr*>(hostent->h_addr_list[i]);
        auto instance = new Address::Ipv4Instance(&address);

        char buffer[INET_ADDRSTRLEN];
        ares_inet_ntop(AF_INET, &(address.sin_addr), buffer, INET_ADDRSTRLEN);
        auto key = std::string(buffer);
        if (ttl.count(key)) {
          instance->setAddrTtl(std::chrono::seconds(ttl[key]));
        }

        address_list.emplace_back(Address::InstanceConstSharedPtr(instance));
      }
    } else if (hostent->h_addrtype == AF_INET6) {
      auto addrttls_ = static_cast<ares_addr6ttl*>(addrttls);
      std::unordered_map<std::string, std::chrono::seconds> ttl;

      for (int i = 0; i < naddrttls; ++i) {
        char buffer[INET6_ADDRSTRLEN];
        ares_inet_ntop(AF_INET6, &(addrttls_[i].ip6addr), buffer, INET6_ADDRSTRLEN);
        ttl[std::string(buffer)] = std::chrono::seconds(addrttls_[i].ttl);
      }

      for (int i = 0; hostent->h_addr_list[i] != nullptr; ++i) {
        ASSERT(hostent->h_length == sizeof(in6_addr));
        sockaddr_in6 address;
        memset(&address, 0, sizeof(address));
        address.sin6_family = AF_INET6;
        address.sin6_port = 0;
        address.sin6_addr = *reinterpret_cast<in6_addr*>(hostent->h_addr_list[i]);
        auto instance = new Address::Ipv6Instance(address);

        char buffer[INET6_ADDRSTRLEN];
        ares_inet_ntop(AF_INET6, &(address.sin6_addr), buffer, INET6_ADDRSTRLEN);
        auto key = std::string(buffer);
        if (ttl.count(key)) {
          instance->setAddrTtl(std::chrono::seconds(ttl[key]));
        }

        address_list.emplace_back(Address::InstanceConstSharedPtr(instance));
      }
    }
    if (!address_list.empty()) {
      completed_ = true;
    }
  }

  if (timeouts > 0) {
    ENVOY_LOG(debug, "DNS request timed out {} times", timeouts);
  }

  if (completed_) {
    if (!cancelled_) {
      try {
        callback_(std::move(address_list));
      } catch (const EnvoyException& e) {
        ENVOY_LOG(critical, "EnvoyException in c-ares callback");
        dispatcher_.post([s = std::string(e.what())] { throw EnvoyException(s); });
      } catch (const std::exception& e) {
        ENVOY_LOG(critical, "std::exception in c-ares callback");
        dispatcher_.post([s = std::string(e.what())] { throw EnvoyException(s); });
      } catch (...) {
        ENVOY_LOG(critical, "Unknown exception in c-ares callback");
        dispatcher_.post([] { throw EnvoyException("unknown"); });
      }
    }
    if (owned_) {
      delete this;
      return;
    }
  }

  if (!completed_ && fallback_if_failed_) {
    fallback_if_failed_ = false;
    getHostByName(AF_INET);
    // Note: Nothing can follow this call to getHostByName due to deletion of this
    // object upon synchronous resolution.
    return;
  }
}

void DnsResolverImpl::updateAresTimer() {
  // Update the timeout for events.
  timeval timeout;
  timeval* timeout_result = ares_timeout(channel_, nullptr, &timeout);
  if (timeout_result != nullptr) {
    const auto ms =
        std::chrono::milliseconds(timeout_result->tv_sec * 1000 + timeout_result->tv_usec / 1000);
    ENVOY_LOG(debug, "Setting DNS resolution timer for {} milliseconds", ms.count());
    timer_->enableTimer(ms);
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
    events_[fd] = dispatcher_.createFileEvent(
        fd, [this, fd](uint32_t events) { onEventCallback(fd, events); },
        Event::FileTriggerType::Level, Event::FileReadyType::Read | Event::FileReadyType::Write);
  }
  events_[fd]->setEnabled((read ? Event::FileReadyType::Read : 0) |
                          (write ? Event::FileReadyType::Write : 0));
}

ActiveDnsQuery* DnsResolverImpl::resolve(const std::string& dns_name,
                                         DnsLookupFamily dns_lookup_family, ResolveCb callback) {
  // TODO(hennna): Add DNS caching which will allow testing the edge case of a
  // failed initial call to getHostByName followed by a synchronous IPv4
  // resolution.
  std::unique_ptr<PendingResolution> pending_resolution(
      new PendingResolution(callback, dispatcher_, channel_, dns_name));
  if (dns_lookup_family == DnsLookupFamily::Auto) {
    pending_resolution->fallback_if_failed_ = true;
  }

  if (dns_lookup_family == DnsLookupFamily::V4Only) {
    pending_resolution->getHostByName(AF_INET);
  } else {
    pending_resolution->getHostByName(AF_INET6);
  }

  if (pending_resolution->completed_) {
    // Resolution does not need asynchronous behavior or network events. For
    // example, localhost lookup.
    return nullptr;
  } else {
    // Enable timer to wake us up if the request times out.
    updateAresTimer();

    // The PendingResolution will self-delete when the request completes
    // (including if cancelled or if ~DnsResolverImpl() happens).
    pending_resolution->owned_ = true;
    return pending_resolution.release();
  }
}

void DnsResolverImpl::PendingResolution::getHostByName(int family) {
  ares_wrapper_.GetHostbyName(
      channel_, dns_name_, family,
      [](void* arg, int status, int timeouts, hostent* hostent, void* addrttls, int naddrttls) {
        static_cast<PendingResolution*>(arg)->onAresHostCallback(status, timeouts, hostent,
                                                                 addrttls, naddrttls);
      },
      this);
}

void DnsResolverImpl::AresWrapper::GetHostbyName(ares_channel channel, const std::string& name,
                                                 int family, const AresHostCallback& callback,
                                                 void* arg) const {
  // Right now we only know how to look up Internet addresses.
  switch (family) {
  case AF_INET:
  case AF_INET6:
    break;
  default:
    callback(arg, ARES_ENOTIMP, 0, nullptr, nullptr, 0);
    return;
  }

  // Per RFC 7686, reject queries for ".onion" domain names.
  if (isOnionDomain(name)) {
    callback(arg, ARES_ENOTFOUND, 0, nullptr, nullptr, 0);
  }

  if (fakeHostent(name, family, callback, arg)) {
    return;
  }

  // TODO(crazyxy): "fb" flag is hard-coded since we cannot access `channel->lookups` here.
  auto hquery = new HostQuery(channel, name, family, -1, callback, arg, "fb", 0);

  // Start performing lookups according to channel->lookups.
  nextLookup(hquery, ARES_ECONNREFUSED);
}

void DnsResolverImpl::AresWrapper::nextLookup(HostQuery* hquery, int statusCode) {
  const char* p;
  hostent* host;
  int status = statusCode;

  for (p = hquery->remaining_lookups_; *p; p++) {
    switch (*p) {
    case 'b':
      // DNS lookup
      hquery->remaining_lookups_ = p + 1;

      if (hquery->want_family_ == AF_INET6) {
        hquery->sent_family_ = AF_INET6;
        ares_search(hquery->channel_, hquery->name_.c_str(), C_IN, T_AAAA, hostCallback, hquery);
      } else {
        hquery->sent_family_ = AF_INET;
        ares_search(hquery->channel_, hquery->name_.c_str(), C_IN, T_A, hostCallback, hquery);
      }

      return;

    case 'f':
      // Host file lookup
      status = ares_gethostbyname_file(hquery->channel_, hquery->name_.c_str(),
                                       hquery->want_family_, &host);

      if (status == ARES_SUCCESS) {
        endHquery(hquery, status, host, nullptr, 0);
        return;
      }

      // Use original status code
      status = statusCode;
      break;
    }
  }
  endHquery(hquery, status, nullptr, nullptr, 0);
}

void DnsResolverImpl::AresWrapper::hostCallback(void* arg, int status, int timeouts,
                                                unsigned char* abuf, int alen) {
  auto hquery = static_cast<HostQuery*>(arg);
  hostent* host = nullptr;
  void* addrttls = nullptr;
  int naddrttls = addrttl_len_;

  hquery->timeouts_ += timeouts;
  if (status == ARES_SUCCESS) {
    if (hquery->sent_family_ == AF_INET) {
      addrttls = new ares_addrttl[naddrttls];
      status =
          ares_parse_a_reply(abuf, alen, &host, static_cast<ares_addrttl*>(addrttls), &naddrttls);
    } else if (hquery->sent_family_ == AF_INET6) {
      addrttls = new ares_addr6ttl[naddrttls];
      status = ares_parse_aaaa_reply(abuf, alen, &host, static_cast<ares_addr6ttl*>(addrttls),
                                     &naddrttls);
      if ((status == ARES_ENODATA || status == ARES_EBADRESP ||
           (status == ARES_SUCCESS && host && host->h_addr_list[0] == nullptr)) &&
          hquery->want_family_ == AF_UNSPEC) {
        // The query returned something but either there were no AAAA records (e.g. just CNAME) or
        // the response was malformed. Try looking up A instead.
        if (host) {
          ares_free_hostent(host);
        }
        delete[] static_cast<ares_addr6ttl*>(addrttls);

        hquery->sent_family_ = AF_INET;
        ares_search(hquery->channel_, hquery->name_.c_str(), C_IN, T_A, hostCallback, hquery);
        return;
      }
    }
    endHquery(hquery, status, host, addrttls, naddrttls);
  } else if ((status == ARES_ENODATA || status == ARES_EBADRESP || status == ARES_ETIMEOUT) &&
             (hquery->sent_family_ == AF_INET6 && hquery->want_family_ == AF_UNSPEC)) {
    // The AAAA query yielded no useful result. Now look up an A instead.
    hquery->sent_family_ = AF_INET;
    ares_search(hquery->channel_, hquery->name_.c_str(), C_IN, T_A, hostCallback, hquery);
  } else if (status == ARES_EDESTRUCTION) {
    endHquery(hquery, status, nullptr, nullptr, 0);
  } else {
    nextLookup(hquery, status);
  }
}

void DnsResolverImpl::AresWrapper::endHquery(HostQuery* hquery, int status, hostent* host,
                                             void* addrttls, int naddrttls) {
  hquery->callback_(hquery->arg_, status, hquery->timeouts_, host, addrttls, naddrttls);
  if (host != nullptr) {
    ares_free_hostent(host);
  }

  if (addrttls != nullptr) {
    if (hquery->sent_family_ == AF_INET) {
      delete[] static_cast<ares_addrttl*>(addrttls);
    } else {
      delete[] static_cast<ares_addr6ttl*>(addrttls);
    }
  }

  delete hquery;
}

bool DnsResolverImpl::AresWrapper::fakeHostent(const std::string& name, int family,
                                               AresHostCallback callback, void* arg) {
  struct hostent hostent;
  char* aliases[1] = {nullptr};
  char* addrs[2];
  int result = 0;
  struct in_addr in;
  struct ares_in6_addr in6;

  if (family == AF_INET || family == AF_INET6) {
    // It only looks like an IP address if it's all numbers and dots.
    int numdots = 0, valid = 1;
    for (auto c : name) {
      if (!isdigit(c) && c != '.') {
        valid = 0;
        break;
      } else if (c == '.') {
        numdots++;
      }
    }

    if (numdots != 3 || !valid) {
      result = 0;
    } else {
      result = (ares_inet_pton(AF_INET, name.c_str(), &in) < 1 ? 0 : 1);
    }

    if (result) {
      family = AF_INET;
    }
  }

  if (family == AF_INET6) {
    result = (ares_inet_pton(AF_INET6, name.c_str(), &in6) < 1 ? 0 : 1);
  }

  if (!result) {
    return false;
  }

  if (family == AF_INET) {
    hostent.h_length = static_cast<int>(sizeof(struct in_addr));
    addrs[0] = reinterpret_cast<char*>(&in);
  } else if (family == AF_INET6) {
    hostent.h_length = static_cast<int>(sizeof(struct ares_in6_addr));
    addrs[0] = reinterpret_cast<char*>(&in6);
  }

  hostent.h_name = strdup(name.c_str());
  if (!hostent.h_name) {
    callback(arg, ARES_ENOMEM, 0, nullptr, nullptr, 0);
    return true;
  }

  // Fill in the rest of the host structure and terminate the query.
  addrs[1] = nullptr;
  hostent.h_aliases = aliases;
  hostent.h_addrtype = family;
  hostent.h_addr_list = addrs;
  callback(arg, ARES_SUCCESS, 0, &hostent, nullptr, 0);

  free(static_cast<char*>(hostent.h_name));
  return true;
}

bool DnsResolverImpl::AresWrapper::isOnionDomain(const std::string& name) {
  auto endWith = [&](const std::string& suffix) -> bool {
    int i = name.size(), j = suffix.size();
    while (i >= 0 && j >= 0) {
      if (std::tolower(name[i]) != suffix[j]) {
        return false;
      }
      i--;
      j--;
    }
    return i >= 0;
  };

  if (endWith(".onion") || endWith(".onion.")) {
    return true;
  }

  return false;
}

} // namespace Network
} // namespace Envoy
