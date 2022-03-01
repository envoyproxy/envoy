#include "source/extensions/filters/udp/udp_proxy/udp_access_log.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

UdpFileAccessLog::UdpFileAccessLog(const Filesystem::FilePathAndType& access_log_file_info,
                                   AccessLog::AccessLogManager& log_manager) {
  log_file_ = log_manager.createAccessLog(access_log_file_info);
}

void UdpFileAccessLog::log(std::shared_ptr<UdpSessionStats> udp_stats){

  static const std::string log_format =
      "{{\"time\": \"{}\"}}\n";

  std::string log_line =
      fmt::format(log_format, AccessLogDateTimeFormatter::fromTime(udp_stats->start_time));

  log_file_->write(log_line);
}

UdpInstanceSharedPtr createUdpAccessLogInstance(
    const std::string& config,
    Server::Configuration::CommonFactoryContext& context) {

  Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, config};
  return std::make_shared<UdpFileAccessLog>(file_info, context.accessLogManager());
}

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy