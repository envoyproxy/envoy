#pragma once

#include "extensions/filters/network/kafka/external/serialization_composite.h"
#include "extensions/filters/network/kafka/serialization.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Represents Kafka response metadata: expected api key, version and correlation id.
 * @see http://kafka.apache.org/protocol.html#protocol_messages
 */
struct ResponseMetadata {
  ResponseMetadata(const int16_t api_key, const int16_t api_version, const int32_t correlation_id)
      : api_key_{api_key}, api_version_{api_version}, correlation_id_{correlation_id} {};

  bool operator==(const ResponseMetadata& rhs) const {
    return api_key_ == rhs.api_key_ && api_version_ == rhs.api_version_ &&
           correlation_id_ == rhs.correlation_id_;
  };

  const int16_t api_key_;
  const int16_t api_version_;
  const int32_t correlation_id_;
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
    return context.computeSize(metadata_.correlation_id_) + context.computeSize(data_);
  }

  /**
   * Encodes given response into a buffer, with any extra configuration carried by the context.
   */
  uint32_t encode(Buffer::Instance& dst) const override {
    EncodingContext context{metadata_.api_version_};
    uint32_t written{0};
    // Encode correlation id (api key / version are not present in responses).
    written += context.encode(metadata_.correlation_id_, dst);
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
