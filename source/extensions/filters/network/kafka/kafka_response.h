#pragma once

#include "extensions/filters/network/kafka/external/serialization_composite.h"
#include "extensions/filters/network/kafka/serialization.h"
#include "extensions/filters/network/kafka/tagged_fields.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Decides if response with given api key & version should have tagged fields in header.
 * Bear in mind, that ApiVersions responses DO NOT contain tagged fields in header (despite having
 * flexible versions) as per
 * https://github.com/apache/kafka/blob/2.4.0/clients/src/main/resources/common/message/ApiVersionsResponse.json#L24
 * This method gets implemented in generated code through 'kafka_response_resolver_cc.j2'.
 *
 * @param api_key Kafka request key.
 * @param api_version Kafka request's version.
 * @return Whether tagged fields should be used for this request.
 */
bool responseUsesTaggedFieldsInHeader(const uint16_t api_key, const uint16_t api_version);

/**
 * Represents Kafka response metadata: expected api key, version and correlation id.
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
struct ResponseMetadata {
  ResponseMetadata(const int16_t api_key, const int16_t api_version, const int32_t correlation_id)
      : ResponseMetadata{api_key, api_version, correlation_id, TaggedFields{}} {};

  ResponseMetadata(const int16_t api_key, const int16_t api_version, const int32_t correlation_id,
                   const TaggedFields& tagged_fields)
      : api_key_{api_key}, api_version_{api_version}, correlation_id_{correlation_id},
        tagged_fields_{tagged_fields} {};

  uint32_t computeSize(const EncodingContext& context) const {
    uint32_t result{0};
    result += context.computeSize(correlation_id_);
    if (responseUsesTaggedFieldsInHeader(api_key_, api_version_)) {
      result += context.computeCompactSize(tagged_fields_);
    }
    return result;
  }

  uint32_t encode(Buffer::Instance& dst, EncodingContext& context) const {
    uint32_t written{0};
    // Encode correlation id (api key / version are not present in responses).
    written += context.encode(correlation_id_, dst);
    if (responseUsesTaggedFieldsInHeader(api_key_, api_version_)) {
      written += context.encodeCompact(tagged_fields_, dst);
    }
    return written;
  }

  bool operator==(const ResponseMetadata& rhs) const {
    return api_key_ == rhs.api_key_ && api_version_ == rhs.api_version_ &&
           correlation_id_ == rhs.correlation_id_ && tagged_fields_ == rhs.tagged_fields_;
  };

  const int16_t api_key_;
  const int16_t api_version_;
  const int32_t correlation_id_;
  const TaggedFields tagged_fields_;
};

using ResponseMetadataSharedPtr = std::shared_ptr<ResponseMetadata>;

/**
 * Abstract response object, carrying data related to every response.
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
class AbstractResponse {
public:
  virtual ~AbstractResponse() = default;

  /**
   * Constructs a request with given metadata.
   * @param metadata response metadata.
   */
  AbstractResponse(const ResponseMetadata& metadata) : metadata_{metadata} {};

  /**
   * Computes the size of this response, if it were to be serialized.
   * @return serialized size of response.
   */
  virtual uint32_t computeSize() const PURE;

  /**
   * Encode the contents of this response into a given buffer.
   * @param dst buffer instance to keep serialized message.
   */
  virtual uint32_t encode(Buffer::Instance& dst) const PURE;

  /**
   * Response's metadata.
   */
  const ResponseMetadata metadata_;
};

using AbstractResponseSharedPtr = std::shared_ptr<AbstractResponse>;

/**
 * Concrete response that carries data particular to given response type.
 * @param Data concrete response data type.
 */
template <typename Data> class Response : public AbstractResponse {
public:
  Response(const ResponseMetadata& metadata, const Data& data)
      : AbstractResponse{metadata}, data_{data} {};

  /**
   * Compute the size of response, which includes both the response header (correlation id) and
   * real data.
   */
  uint32_t computeSize() const override {
    const EncodingContext context{metadata_.api_version_};
    uint32_t result{0};
    // Compute size of header.
    result += context.computeSize(metadata_);
    // Compute size of response data.
    result += context.computeSize(data_);
    return result;
  }

  /**
   * Encodes given response into a buffer, with any extra configuration carried by the context.
   */
  uint32_t encode(Buffer::Instance& dst) const override {
    EncodingContext context{metadata_.api_version_};
    uint32_t written{0};
    // Encode response header.
    written += context.encode(metadata_, dst);
    // Encode response-specific data.
    written += context.encode(data_, dst);
    return written;
  }

  bool operator==(const Response<Data>& rhs) const {
    return metadata_ == rhs.metadata_ && data_ == rhs.data_;
  };

private:
  const Data data_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
