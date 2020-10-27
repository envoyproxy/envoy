#pragma once

#include "common/network/apple_dns_impl.h"

#include "gmock/gmock.h"

namespace Envoy {
namespace Network {

class MockDnsService : public DnsService {
public:
  MockDnsService() = default;
  ~MockDnsService() = default;

  MOCK_METHOD(void, dnsServiceRefDeallocate, (DNSServiceRef sdRef));
  MOCK_METHOD(DNSServiceErrorType, dnsServiceCreateConnection, (DNSServiceRef * sdRef));
  MOCK_METHOD(dnssd_sock_t, dnsServiceRefSockFD, (DNSServiceRef sdRef));
  MOCK_METHOD(DNSServiceErrorType, dnsServiceProcessResult, (DNSServiceRef sdRef));
  MOCK_METHOD(DNSServiceErrorType, dnsServiceGetAddrInfo,
              (DNSServiceRef * sdRef, DNSServiceFlags flags, uint32_t interfaceIndex,
               DNSServiceProtocol protocol, const char* hostname,
               DNSServiceGetAddrInfoReply callBack, void* context));
};

} // namespace Network
} // namespace Envoy
