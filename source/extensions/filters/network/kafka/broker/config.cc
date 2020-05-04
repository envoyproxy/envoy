#include "extensions/filters/network/kafka/broker/config.h"

#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/scope.h"

#include "extensions/filters/network/kafka/broker/filter.h"

#include "librdkafka/rdkafka.h"
#include "librdkafka/rdkafkacpp.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

class ExampleDeliveryReportCb : public RdKafka::DeliveryReportCb,
                                private Logger::Loggable<Logger::Id::kafka> {
public:
  void dr_cb(RdKafka::Message& message) {
    if (message.err())
      ENVOY_LOG(warn, "Message delivery failed: {}", message.errstr());
    else
      ENVOY_LOG(warn, "Message delivered: {}", message.partition());
  }
};

Network::FilterFactoryCb KafkaConfigFactory::createFilterFactoryFromProtoTyped(
    const KafkaBrokerProtoConfig& proto_config, Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());

  RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);

  std::string errstr;
  conf->set("bootstrap.servers", "127.0.0.1:9092", errstr);

  ExampleDeliveryReportCb ex_dr_cb;
  conf->set("dr_cb", &ex_dr_cb, errstr);

  RdKafka::Producer* producer = RdKafka::Producer::create(conf, errstr);
  if (producer) {
    ENVOY_LOG(warn, "producer is alive");
  }

  std::string line = "aaaabbbbCCCCCdddd";
  const auto p_res =
      producer->produce("apples", RdKafka::Topic::PARTITION_UA, RdKafka::Producer::RK_MSG_COPY,
                        const_cast<char*>(line.c_str()), line.size(), NULL, 0, 0, NULL, NULL);
  ENVOY_LOG(warn, "p_res = {}", p_res);

  ENVOY_LOG(warn, "poll start");
  producer->poll(10000);
  ENVOY_LOG(warn, "poll finished");

  const std::string& stat_prefix = proto_config.stat_prefix();

  return [&context, stat_prefix](Network::FilterManager& filter_manager) -> void {
    Network::FilterSharedPtr filter =
        std::make_shared<KafkaBrokerFilter>(context.scope(), context.timeSource(), stat_prefix);
    filter_manager.addFilter(filter);
  };
}

/**
 * Static registration for the Kafka filter. @see RegisterFactory.
 */
REGISTER_FACTORY(KafkaConfigFactory, Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
