#include "contrib/kafka/stat_sinks/source/config.h"

#include "envoy/registry/registry.h"

#include "source/common/common/assert.h"
#include "source/common/common/logger.h"
#include "source/common/config/utility.h"

#include "contrib/envoy/extensions/stat_sinks/kafka/v3/kafka_stats_sink.pb.h"
#include "contrib/envoy/extensions/stat_sinks/kafka/v3/kafka_stats_sink.pb.validate.h"
#include "contrib/kafka/filters/network/source/mesh/librdkafka_utils_impl.h"
#include "contrib/kafka/stat_sinks/source/kafka_stats_sink_impl.h"

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Kafka {

using NetworkFilters::Kafka::Mesh::LibRdKafkaUtilsImpl;

absl::StatusOr<Stats::SinkPtr>
KafkaStatsSinkFactory::createStatsSink(const Protobuf::Message& config,
                                       Server::Configuration::ServerFactoryContext& server) {
  const auto& sink_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::stat_sinks::kafka::v3::KafkaStatsSinkConfig&>(
      config, server.messageValidationContext().staticValidationVisitor());

  const auto& utils = LibRdKafkaUtilsImpl::getDefaultInstance();

  std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
  std::string errstr;

  if (utils.setConfProperty(*conf, "bootstrap.servers", sink_config.broker_list(), errstr) !=
      RdKafka::Conf::CONF_OK) {
    return absl::InvalidArgumentError(absl::StrCat("Failed to set Kafka broker list: ", errstr));
  }

  const uint32_t linger_ms =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, buffer_flush_timeout_ms, 500);
  if (utils.setConfProperty(*conf, "linger.ms", std::to_string(linger_ms), errstr) !=
      RdKafka::Conf::CONF_OK) {
    return absl::InvalidArgumentError(absl::StrCat("Failed to set linger.ms: ", errstr));
  }

  for (const auto& [key, value] : sink_config.producer_config()) {
    if (utils.setConfProperty(*conf, key, value, errstr) != RdKafka::Conf::CONF_OK) {
      return absl::InvalidArgumentError(
          absl::StrCat("Failed to set Kafka property [", key, "] to [", value, "]: ", errstr));
    }
  }

  auto producer = utils.createProducer(conf.get(), errstr);
  if (!producer) {
    return absl::InternalError(absl::StrCat("Failed to create Kafka producer: ", errstr));
  }

  const bool report_counters_as_deltas =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, report_counters_as_deltas, false);

  SerializationFormat format = SerializationFormat::Json;
  if (sink_config.format() ==
      envoy::extensions::stat_sinks::kafka::v3::SerializationFormat::PROTOBUF) {
    format = SerializationFormat::Protobuf;
  }

  const bool emit_tags_as_labels =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(sink_config, emit_tags_as_labels, true);

  KafkaMetricsFlusher flusher(report_counters_as_deltas, emit_tags_as_labels, format);

  return std::make_unique<KafkaStatsSink>(std::move(producer), sink_config.topic(),
                                          sink_config.batch_size(), std::move(flusher));
}

ProtobufTypes::MessagePtr KafkaStatsSinkFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::stat_sinks::kafka::v3::KafkaStatsSinkConfig>();
}

std::string KafkaStatsSinkFactory::name() const { return KafkaStatsSinkName; }

REGISTER_FACTORY(KafkaStatsSinkFactory, Server::Configuration::StatsSinkFactory);

} // namespace Kafka
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
