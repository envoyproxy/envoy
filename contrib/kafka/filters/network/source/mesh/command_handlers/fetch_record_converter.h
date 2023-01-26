#pragma once

#include <map>
#include <memory>
#include <vector>

#include "contrib/kafka/filters/network/source/external/responses.h"
#include "contrib/kafka/filters/network/source/kafka_types.h"
#include "contrib/kafka/filters/network/source/mesh/inbound_record.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

using InboundRecordsMap = std::map<KafkaPartition, std::vector<InboundRecordSharedPtr>>;

/**
 * Dependency injection class responsible for converting received records into serializable form
 * that we can put into Fetch responses.
 */
class FetchRecordConverter {
public:
  virtual ~FetchRecordConverter() = default;

  // Converts received records into the serialized form.
  virtual std::vector<FetchableTopicResponse> convert(const InboundRecordsMap& arg) const PURE;
};

/**
 * Proper implementation.
 */
class FetchRecordConverterImpl : public FetchRecordConverter {
public:
  // FetchRecordConverter
  std::vector<FetchableTopicResponse> convert(const InboundRecordsMap& arg) const override;

  // Default singleton accessor.
  static const FetchRecordConverter& getDefaultInstance();
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
