#pragma once

#include <memory>

#include "envoy/common/exception.h"

#include "common/common/assert.h"

#include "extensions/filters/network/kafka/kafka_request.h"
#include "extensions/filters/network/kafka/parser.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Request parser responsible for consuming request length and setting up context with this data.
 * @see http://kafka.apache.org/protocol.html#protocol_common
 */
class RequestStartParser : public Parser {
public:
  ParseResponse parse(absl::string_view& data) override;
};

/**
 * Deserializer that extracts request header (4 fields).
 * Can throw, as one of the fields (client-id) can throw (nullable string with invalid length).
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class RequestHeaderDeserializer
    : public CompositeDeserializerWith4Delegates<RequestHeader, Int16Deserializer,
                                                 Int16Deserializer, Int32Deserializer,
                                                 NullableStringDeserializer> {};

typedef std::unique_ptr<RequestHeaderDeserializer> RequestHeaderDeserializerPtr;

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
