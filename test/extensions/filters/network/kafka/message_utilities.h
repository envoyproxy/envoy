#pragma once

#include <vector>

#include "extensions/filters/network/kafka/kafka_request.h"
#include "extensions/filters/network/kafka/kafka_response.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * Payload-related test utilities.
 * This class is intended to be an entry point for all generated methods.
 *
 * The methods declared here are implemented in generated message_utils.cc, as they are derived
 * from Kafka protocol specification.
 */
class MessageUtilities {
private:
  MessageUtilities(){};

public:
  /**
   * How many request/response types are supported.
   * Proper values are 0..apiKeys() - 1.
   */
  static int16_t apiKeys();

  /**
   * How many request types are supported for given api key.
   */
  static int16_t requestApiVersions(const int16_t api_key);

  /**
   * Make example requests with given api_key. One message per api version in given api key.
   */
  static std::vector<AbstractRequestSharedPtr> makeRequests(const int16_t api_key);

  /**
   * Make example requests, one message per given api key + api version pair.
   */
  static std::vector<AbstractRequestSharedPtr> makeAllRequests();

  /**
   * Get the name of request counter metric for given request type.
   */
  static std::string requestMetric(const int16_t api_key);

  /**
   * How many response types are supported for given api key.
   */
  static int16_t responseApiVersions(const int16_t api_key);

  /**
   * Make example requests with given api_key. One message per api version in given api key.
   */
  static std::vector<AbstractResponseSharedPtr> makeResponses(const int16_t api_key);

  /**
   * Make example responses, one message per given api key + api version pair.
   */
  static std::vector<AbstractResponseSharedPtr> makeAllResponses();

  /**
   * Get the name of response counter metric for given request type.
   */
  static std::string responseMetric(const int16_t api_key);
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
