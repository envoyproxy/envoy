#include "source/extensions/network/dns_resolver/common/res_query.h"

#include <arpa/nameser.h>
#include <netinet/in.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/network/address_impl.h"

namespace Envoy {
namespace Network {

static const int kHttpsRecordType = 65;

absl::StatusOr<LookupResult> doSrvLookup(absl::string_view domain, int32_t port) {
  port = ntohs(port);

  // Kick off a query for the specified domain.
  unsigned char res_result_buffer[4096];
  int msglen =
      res_query(domain.data(), ns_t_a, ns_t_any, res_result_buffer, sizeof(res_result_buffer));
  if (msglen <= 0) {
    ENVOY_LOG_MISC(trace, "res_query call failed");
    return absl::UnknownError("res_query call failed");
  }

  LookupResult result;
  ns_msg msg;

  if (ns_initparse(res_result_buffer, msglen, &msg) != 0) {
    ENVOY_LOG_MISC(trace, "ns_initparse call failed");
    return absl::UnknownError("ns_initparse call failed");
  }

  int num_resolutions = ns_msg_count(msg, ns_s_an);
  for (int i = 0; i < num_resolutions; i++) {
    // Parse each individual record, or continue if the record is invalid.
    ns_rr record;
    if (ns_parserr(&msg, ns_s_an, i, &record) != 0) {
      ENVOY_LOG_MISC(trace, "ns parse error");
      continue;
    }

    // Snag the record type, and only handle it if it's A, AAAA or HTTPS.
    int record_type = ns_rr_type(record);
    if (record_type != ns_t_a && record_type != ns_t_aaaa && record_type != kHttpsRecordType) {
      continue;
    }

    // TODO(alyssawilk) use TTL

    // Get the record as a string view.
    absl::string_view record_data(reinterpret_cast<const char*>(ns_rr_rdata(record)),
                                  ns_rr_rdlen(record));

    if (record_type == ns_t_a) {
      if (record_data.size() != 4) {
        ENVOY_LOG_MISC(trace, "Bad A record for {}", domain);
        continue;
      }
      sockaddr_in addr;
      addr.sin_addr.s_addr = *(reinterpret_cast<const in_addr_t*>(record_data.data()));
      addr.sin_port = port;
      auto status_or_address =
          Address::InstanceFactory::createInstancePtr<Address::Ipv4Instance>(&addr);
      if (!status_or_address.ok()) {
        ENVOY_LOG_MISC(trace, "Failed to parse address for {}", domain);
        continue;
      }
      ENVOY_LOG_MISC(trace, "res query resolved {} for domain {}", (*status_or_address)->asString(),
                     domain);
      result.resolved_addresses.push_back(*status_or_address);
      continue;
    } else if (record_type == ns_t_aaaa) {
      if (record_data.size() != 16) {
        ENVOY_LOG_MISC(trace, "Bad AAA record for {}", domain);
        continue;
      }
      sockaddr_in6 sin6;
      sin6.sin6_family = AF_INET6;
      sin6.sin6_addr = *(reinterpret_cast<const in6_addr*>(record_data.data()));
      sin6.sin6_port = port;
      auto status_or_address =
          Address::InstanceFactory::createInstancePtr<Address::Ipv6Instance>(sin6);
      if (!status_or_address.ok()) {
        ENVOY_LOG_MISC(trace, "Failed to parse address for {}", domain);
        continue;
      }
      ENVOY_LOG_MISC(trace, "res query resolved {} for domain {}", (*status_or_address)->asString(),
                     domain);
      result.resolved_addresses.push_back(*status_or_address);
      continue;
    } else if (record_type == kHttpsRecordType) {
      // TODO(alyssawilk) handle https record types.
    }
  }
  if (result.resolved_addresses.size() > 0) {
    return result;
  }

  ENVOY_LOG_MISC(trace, "res query resolved no addresses for domain {}", domain);
  return absl::UnknownError("No addresses resolved");
}

} // namespace Network
} // namespace Envoy
