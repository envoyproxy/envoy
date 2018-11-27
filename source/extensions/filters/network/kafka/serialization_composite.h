#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "envoy/buffer/buffer.h"
#include "envoy/common/exception.h"
#include "envoy/common/pure.h"

#include "common/common/byte_order.h"
#include "common/common/fmt.h"

#include "extensions/filters/network/kafka/kafka_types.h"
#include "extensions/filters/network/kafka/serialization.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {

/**
 * This header contains only composite deserializers
 * The basic design is composite deserializer creating delegates DeserializerType1..Tn
 * Result of type ResponseType is constructed by getting results of each of delegates
 */

/**
 * Composite deserializer that uses 2 deserializers
 * Passes data to each of the underlying deserializers
 * (deserializers that are already ready do not consume data, so it's safe).
 * The composite deserializer is ready when the last deserializer is ready
 * (which means all deserializers before it are ready too)
 * Constructs the result of type ResponseType using { delegate1_.get(), delegate2_.get() ... }
 *
 * @param ResponseType type of deserialized data
 * @param DeserializerType1 1st deserializer (result used as 1st argument of ResponseType's ctor)
 * @param DeserializerType2 2nd deserializer (result used as 2nd argument of ResponseType's ctor)
 */
template <typename ResponseType, typename DeserializerType1, typename DeserializerType2>
class CompositeDeserializerWith2Delegates : public Deserializer<ResponseType> {
public:
  CompositeDeserializerWith2Delegates(){};
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += delegate1_.feed(buffer, remaining);
    consumed += delegate2_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const { return delegate2_.ready(); }
  ResponseType get() const { return {delegate1_.get(), delegate2_.get()}; }

protected:
  DeserializerType1 delegate1_;
  DeserializerType2 delegate2_;
};

/**
 * Composite deserializer that uses 3 deserializers
 * Passes data to each of the underlying deserializers
 * (deserializers that are already ready do not consume data, so it's safe).
 * The composite deserializer is ready when the last deserializer is ready
 * (which means all deserializers before it are ready too)
 * Constructs the result of type ResponseType using { delegate1_.get(), delegate2_.get() ... }
 *
 * @param ResponseType type of deserialized data
 * @param DeserializerType1 1st deserializer (result used as 1st argument of ResponseType's ctor)
 * @param DeserializerType2 2nd deserializer (result used as 2nd argument of ResponseType's ctor)
 * @param DeserializerType3 3rd deserializer (result used as 3rd argument of ResponseType's ctor)
 */
template <typename ResponseType, typename DeserializerType1, typename DeserializerType2,
          typename DeserializerType3>
class CompositeDeserializerWith3Delegates : public Deserializer<ResponseType> {
public:
  CompositeDeserializerWith3Delegates(){};
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += delegate1_.feed(buffer, remaining);
    consumed += delegate2_.feed(buffer, remaining);
    consumed += delegate3_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const { return delegate3_.ready(); }
  ResponseType get() const { return {delegate1_.get(), delegate2_.get(), delegate3_.get()}; }

protected:
  DeserializerType1 delegate1_;
  DeserializerType2 delegate2_;
  DeserializerType3 delegate3_;
};

/**
 * Composite deserializer that uses 4 deserializers
 * Passes data to each of the underlying deserializers
 * (deserializers that are already ready do not consume data, so it's safe).
 * The composite deserializer is ready when the last deserializer is ready
 * (which means all deserializers before it are ready too)
 * Constructs the result of type ResponseType using { delegate1_.get(), delegate2_.get() ... }
 *
 * @param ResponseType type of deserialized data
 * @param DeserializerType1 1st deserializer (result used as 1st argument of ResponseType's ctor)
 * @param DeserializerType2 2nd deserializer (result used as 2nd argument of ResponseType's ctor)
 * @param DeserializerType3 3rd deserializer (result used as 3rd argument of ResponseType's ctor)
 * @param DeserializerType4 4th deserializer (result used as 4th argument of ResponseType's ctor)
 */
template <typename ResponseType, typename DeserializerType1, typename DeserializerType2,
          typename DeserializerType3, typename DeserializerType4>
class CompositeDeserializerWith4Delegates : public Deserializer<ResponseType> {
public:
  CompositeDeserializerWith4Delegates(){};
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += delegate1_.feed(buffer, remaining);
    consumed += delegate2_.feed(buffer, remaining);
    consumed += delegate3_.feed(buffer, remaining);
    consumed += delegate4_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const { return delegate4_.ready(); }
  ResponseType get() const {
    return {delegate1_.get(), delegate2_.get(), delegate3_.get(), delegate4_.get()};
  }

protected:
  DeserializerType1 delegate1_;
  DeserializerType2 delegate2_;
  DeserializerType3 delegate3_;
  DeserializerType4 delegate4_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
