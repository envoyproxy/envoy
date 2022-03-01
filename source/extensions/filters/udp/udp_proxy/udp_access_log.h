#pragma once

#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.h"
#include "envoy/extensions/filters/udp/udp_proxy/v3/udp_proxy.pb.validate.h"
#include "envoy/server/filter_config.h"
#include "source/extensions/access_loggers/common/access_log_base.h"

#include "source/common/formatter/substitution_formatter.h"


namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {


/**
 * Struct definition for UDP Session access logging.
 */
struct UdpSessionStats {
  SystemTime start_time;
  uint64_t downstream_sess_rx_bytes;
  uint64_t downstream_sess_rx_errors;
  uint64_t downstream_sess_rx_datagrams;
  uint64_t downstream_sess_tx_bytes;
  uint64_t downstream_sess_tx_errors;
  uint64_t downstream_sess_tx_datagrams;
  uint64_t downstream_sess_total;
  uint64_t idle_timeout;

  uint64_t upstream_sess_rx_bytes;
  uint64_t upstream_sess_rx_errors;
  uint64_t upstream_sess_rx_datagrams;
  uint64_t upstream_sess_tx_bytes;
  uint64_t upstream_sess_tx_errors;
  uint64_t upstream_sess_tx_datagrams;
  uint64_t upstream_overflow;
  uint64_t upstream_none_healthy;
  uint64_t upstream_datagrams_dropped;
};

/**
 * Access log Instance of UDP proxy that writes logs to a file.
 */
class UdpFileAccessLog {
public:
  UdpFileAccessLog(const Filesystem::FilePathAndType& access_log_file_info,
                  Formatter::FormatterPtr&& formatter, AccessLog::AccessLogManager& log_manager);

  void log(std::shared_ptr<UdpSessionStats> udp_stats);
private:

  AccessLog::AccessLogFileSharedPtr log_file_;
  Formatter::FormatterPtr formatter_;
};

using UdpInstanceSharedPtr = std::shared_ptr<UdpFileAccessLog>;

UdpInstanceSharedPtr createUdpAccessLogInstance(
    const Protobuf::Message& config,
    Server::Configuration::CommonFactoryContext& context);

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy