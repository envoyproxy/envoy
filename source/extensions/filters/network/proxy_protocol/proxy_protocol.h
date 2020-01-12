#pragma once

#include "envoy/buffer/buffer.h"
#include "envoy/extensions/filters/network/proxy_protocol/v3/proxy_protocol.pb.h"
#include "envoy/network/filter.h"

#include "common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ProxyProtocol {

/**
 * Proxy protocol header generation upstream filter.
 */
class Filter : public Network::WriteFilter, Logger::Loggable<Logger::Id::filter> {
public:
  Filter(const envoy::extensions::filters::network::proxy_protocol::v3::ProxyProtocol& config);
  Network::FilterStatus onWrite(Buffer::Instance& data, bool end_stream) override;
  void initializeWriteFilterCallbacks(Network::WriteFilterCallbacks&) override;

private:
  void injectHeader();
  void injectHeaderV1();
  void injectHeaderV2();

  Network::WriteFilterCallbacks* write_callbacks_{};
  bool sent_header_{false};
  const envoy::extensions::filters::network::proxy_protocol::v3::ProxyProtocol config_;
};

} // namespace ProxyProtocol
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
