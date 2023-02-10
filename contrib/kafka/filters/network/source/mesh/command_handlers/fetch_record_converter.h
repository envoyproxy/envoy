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

  static uint32_t computeCrc32cForTest(const unsigned char* data, const size_t len);

private:
  // Helper function: transform records from a partition into a record batch.
  // See: https://kafka.apache.org/33/documentation.html#recordbatch
  Bytes renderRecordBatch(const std::vector<InboundRecordSharedPtr>& records) const;

  // Helper function: append record to output array.
  // See: https://kafka.apache.org/33/documentation.html#record
  // https://github.com/apache/kafka/blob/3.3.2/clients/src/main/java/org/apache/kafka/common/record/DefaultRecord.java#L164
  void appendRecord(const InboundRecord& record, Bytes& out) const;

  // Helper function: render CRC32C bytes from given input.
  Bytes renderCrc32c(const unsigned char* data, const size_t len) const;

  // Helper function: compute CRC32C.
  static uint32_t computeCrc32c(const unsigned char* data, const size_t len);
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
