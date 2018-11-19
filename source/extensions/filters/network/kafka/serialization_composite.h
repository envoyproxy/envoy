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
 * The basic design is composite deserializer creating delegates T1..Tn
 * Result of type RT is constructed by getting results of each of delegates
 */

/**
 * Composite deserializer that uses 2 deserializers
 * Passes data to each of the underlying deserializers
 * (deserializers that are already ready do not consume data, so it's safe).
 * The composite deserializer is ready when the last deserializer is ready
 * (which means all deserializers before it are ready too)
 * Constructs the result of type RT using { delegate1_.get(), delegate2_.get() ... }
 *
 * @param RT type of deserialized data
 * @param T1 1st deserializer (result used as 1st argument of RT's ctor)
 * @param T2 2nd deserializer (result used as 2nd argument of RT's ctor)
 */
template <typename RT, typename T1, typename T2>
class CompositeDeserializerWith2Delegates : public Deserializer<RT> {
public:
  CompositeDeserializerWith2Delegates(){};
  size_t feed(const char*& buffer, uint64_t& remaining) {
    size_t consumed = 0;
    consumed += delegate1_.feed(buffer, remaining);
    consumed += delegate2_.feed(buffer, remaining);
    return consumed;
  }
  bool ready() const { return delegate2_.ready(); }
  RT get() const { return {delegate1_.get(), delegate2_.get()}; }

protected:
  T1 delegate1_;
  T2 delegate2_;
};

/**
 * Composite deserializer that uses 3 deserializers
 * Passes data to each of the underlying deserializers
 * (deserializers that are already ready do not consume data, so it's safe).
 * The composite deserializer is ready when the last deserializer is ready
 * (which means all deserializers before it are ready too)
 * Constructs the result of type RT using { delegate1_.get(), delegate2_.get() ... }
 *
 * @param RT type of deserialized data
 * @param T1 1st deserializer (result used as 1st argument of RT's ctor)
 * @param T2 2nd deserializer (result used as 2nd argument of RT's ctor)
 * @param T3 3rd deserializer (result used as 3rd argument of RT's ctor)
 */
template <typename RT, typename T1, typename T2, typename T3>
class CompositeDeserializerWith3Delegates : public Deserializer<RT> {
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
  RT get() const { return {delegate1_.get(), delegate2_.get(), delegate3_.get()}; }

protected:
  T1 delegate1_;
  T2 delegate2_;
  T3 delegate3_;
};

/**
 * Composite deserializer that uses 4 deserializers
 * Passes data to each of the underlying deserializers
 * (deserializers that are already ready do not consume data, so it's safe).
 * The composite deserializer is ready when the last deserializer is ready
 * (which means all deserializers before it are ready too)
 * Constructs the result of type RT using { delegate1_.get(), delegate2_.get() ... }
 *
 * @param RT type of deserialized data
 * @param T1 1st deserializer (result used as 1st argument of RT's ctor)
 * @param T2 2nd deserializer (result used as 2nd argument of RT's ctor)
 * @param T3 3rd deserializer (result used as 3rd argument of RT's ctor)
 * @param T4 4th deserializer (result used as 4th argument of RT's ctor)
 */
template <typename RT, typename T1, typename T2, typename T3, typename T4>
class CompositeDeserializerWith4Delegates : public Deserializer<RT> {
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
  RT get() const {
    return {delegate1_.get(), delegate2_.get(), delegate3_.get(), delegate4_.get()};
  }

protected:
  T1 delegate1_;
  T2 delegate2_;
  T3 delegate3_;
  T4 delegate4_;
};

} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
