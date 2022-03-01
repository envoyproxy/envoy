#include "source/extensions/filters/udp/udp_proxy/udp_access_log.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {

UdpFileAccessLog::UdpFileAccessLog(const Filesystem::FilePathAndType& access_log_file_info,
                                   Formatter::FormatterPtr&& formatter,
                                   AccessLog::AccessLogManager& log_manager)
    : formatter_(std::move(formatter)) {
  log_file_ = log_manager.createAccessLog(access_log_file_info);
}

void UdpFileAccessLog::log(std::shared_ptr<UdpSessionStats> udp_stats){
    (void)udp_stats;
}

UdpInstanceSharedPtr createUdpAccessLogInstance(
    const Protobuf::Message& config,
    Server::Configuration::CommonFactoryContext& context) {

  auto& factory =
      Config::Utility::getAndCheckFactory<Server::Configuration::AccessLogInstanceFactory>(config);
  ProtobufTypes::MessagePtr message = Config::Utility::translateToFactoryConfig(
      config, context.messageValidationVisitor(), factory);
      
  const auto& fal_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::access_loggers::file::v3::FileAccessLog&>(
      config, context.messageValidationVisitor());
  Formatter::FormatterPtr formatter = Formatter::SubstitutionFormatUtils::defaultSubstitutionFormatter();
 

  Filesystem::FilePathAndType file_info{Filesystem::DestinationType::File, fal_config.path()};
  return std::make_shared<UdpFileAccessLog>(file_info, std::move(formatter),
                                         context.accessLogManager());
}

} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy