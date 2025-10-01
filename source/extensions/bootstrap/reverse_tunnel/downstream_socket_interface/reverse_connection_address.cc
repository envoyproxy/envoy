#include "source/extensions/bootstrap/reverse_tunnel/downstream_socket_interface/reverse_connection_address.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cstring>
#include <functional>

#include "source/common/common/fmt.h"

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

static const std::string reverse_connection_address = "127.0.0.1:0";

ReverseConnectionAddress::ReverseConnectionAddress(const ReverseConnectionConfig& config)
    : config_(config) {

  // Create the logical name (rc:// address) for identification.
  logical_name_ = fmt::format("rc://{}:{}:{}@{}:{}", config.src_node_id, config.src_cluster_id,
                              config.src_tenant_id, config.remote_cluster, config.connection_count);

  // Use localhost with a static port for the actual address string to pass IP validation
  // This will be used by the filter chain manager for matching.
  address_string_ = reverse_connection_address;

  ENVOY_LOG_MISC(debug, "reverse connection address: logical_name={}, address={}", logical_name_,
                 address_string_);
}

bool ReverseConnectionAddress::operator==(const Instance& rhs) const {
  const auto* reverse_conn_addr = dynamic_cast<const ReverseConnectionAddress*>(&rhs);
  if (reverse_conn_addr == nullptr) {
    return false;
  }
  return config_.src_node_id == reverse_conn_addr->config_.src_node_id &&
         config_.src_cluster_id == reverse_conn_addr->config_.src_cluster_id &&
         config_.src_tenant_id == reverse_conn_addr->config_.src_tenant_id &&
         config_.remote_cluster == reverse_conn_addr->config_.remote_cluster &&
         config_.connection_count == reverse_conn_addr->config_.connection_count;
}

const std::string& ReverseConnectionAddress::asString() const { return address_string_; }

absl::string_view ReverseConnectionAddress::asStringView() const { return address_string_; }

const std::string& ReverseConnectionAddress::logicalName() const { return logical_name_; }

const sockaddr* ReverseConnectionAddress::sockAddr() const {
  // Return a valid localhost sockaddr structure for IP validation.
  static struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);                      // Port 0
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
  return reinterpret_cast<const sockaddr*>(&addr);
}

socklen_t ReverseConnectionAddress::sockAddrLen() const { return sizeof(struct sockaddr_in); }

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
