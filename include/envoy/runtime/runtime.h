#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "envoy/common/pure.h"

#include "absl/types/optional.h"

namespace Envoy {
namespace Runtime {

/**
 * Random number generator. Implementations should be thread safe.
 */
class RandomGenerator {
public:
  virtual ~RandomGenerator() {}

  /**
   * @return uint64_t a new random number.
   */
  virtual uint64_t random() PURE;

  /**
   * @return std::string containing uuid4 of 36 char length.
   * for example, 7c25513b-0466-4558-a64c-12c6704f37ed
   */
  virtual std::string uuid() PURE;
};

typedef std::unique_ptr<RandomGenerator> RandomGeneratorPtr;

/**
 * A snapshot of runtime data.
 */
class Snapshot {
public:
  virtual ~Snapshot() {}

  /**
   * The raw data from a single snapshot key.
   */
  struct Entry {
    /**
     * The raw runtime data.
     */
    std::string string_value_;

    /**
     * The possibly parsed integer value from the runtime data.
     */
    absl::optional<uint64_t> uint_value_;
  };

  /**
   * A provider of runtime values. One or more of these compose the snapshot's source of values,
   * where successive layers override the previous ones.
   */
  class OverrideLayer {
  public:
    virtual ~OverrideLayer() {}
    /**
     * @return const std::unordered_map<std::string, Entry>& the values in this layer.
     */
    virtual const std::unordered_map<std::string, Entry>& values() const PURE;
    /**
     * @return const std::string& a user-friendly alias for this layer, e.g. "admin" or "disk".
     */
    virtual const std::string& name() const PURE;
  };

  typedef std::unique_ptr<const OverrideLayer> OverrideLayerConstPtr;

  /**
   * Test if a feature is enabled using the built in random generator. This is done by generating
   * a random number in the range 0-99 and seeing if this number is < the value stored in the
   * runtime key, or the default_value if the runtime key is invalid.
   * NOTE: In the current implementation, this routine may return different results each time it is
   *       called because a new random number is used each time. Callers should understand this
   *       behavior and not assume that subsequent calls using the same snapshot will be consistent.
   * @param key supplies the feature key to lookup.
   * @param default_value supplies the default value that will be used if either the feature key
   *        does not exist or it is not an integer.
   * @return true if the feature is enabled.
   */
  virtual bool featureEnabled(const std::string& key, uint64_t default_value) const PURE;

  /**
   * Test if a feature is enabled using a supplied stable random value. This variant is used if
   * the caller wants a stable result over multiple calls.
   * @param key supplies the feature key to lookup.
   * @param default_value supplies the default value that will be used if either the feature key
   *        does not exist or it is not an integer.
   * @param random_value supplies the stable random value to use for determining whether the feature
   *        is enabled.
   * @return true if the feature is enabled.
   */
  virtual bool featureEnabled(const std::string& key, uint64_t default_value,
                              uint64_t random_value) const PURE;

  /**
   * Test if a feature is enabled using a supplied stable random value and total number of buckets
   * for sampling.
   * This variant is used if the caller wants a stable result over multiple calls
   * and have more granularity for samples.
   * @param key supplies the feature key to lookup.
   * @param default_value supplies the default value that will be used if either the feature key
   *        does not exist or it is not an integer.
   * @param random_value supplies the stable random value to use for determining whether the feature
   *        is enabled.
   * @param control max number of buckets for sampling. Sampled value will be in a range of
   *        [0, num_buckets).
   * @return true if the feature is enabled.
   */
  virtual bool featureEnabled(const std::string& key, uint64_t default_value, uint64_t random_value,
                              uint64_t num_buckets) const PURE;

  /**
   * Fetch raw runtime data based on key.
   * @param key supplies the key to fetch.
   * @return const std::string& the value or empty string if the key does not exist.
   */
  virtual const std::string& get(const std::string& key) const PURE;

  /**
   * Fetch an integer runtime key.
   * @param key supplies the key to fetch.
   * @param default_value supplies the value to return if the key does not exist or it does not
   *        contain an integer.
   * @return uint64_t the runtime value or the default value.
   */
  virtual uint64_t getInteger(const std::string& key, uint64_t default_value) const PURE;

  /**
   * Fetch the OverrideLayers that provide values in this snapshot. Layers are ordered from bottom
   * to top; for instance, the second layer's entries override the first layer's entries, and so on.
   * Any layer can add a key in addition to overriding keys in layers below. The layer vector is
   * safe only for the lifetime of the Snapshot.
   * @return const std::vector<OverrideLayerConstPtr>& the raw map of loaded values.
   */
  virtual const std::vector<OverrideLayerConstPtr>& getLayers() const PURE;
};

/**
 * Loads runtime snapshots from storage (local disk, etc.).
 */
class Loader {
public:
  virtual ~Loader() {}

  /**
   * @return Snapshot& the current snapshot. This reference is safe to use for the duration of
   *         the calling routine, but may be overwritten on a future event loop cycle so should be
   *         fetched again when needed.
   */
  virtual Snapshot& snapshot() PURE;

  /**
   * Merge the given map of key-value pairs into the runtime's state. To remove a previous merge for
   * a key, use an empty string as the value.
   * @param values the values to merge
   */
  virtual void mergeValues(const std::unordered_map<std::string, std::string>& values) PURE;
};

typedef std::unique_ptr<Loader> LoaderPtr;

} // namespace Runtime
} // namespace Envoy
