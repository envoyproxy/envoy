#include <memory>
#include <random>
#include <vector>

#include "envoy/common/random_generator.h"

#include "common/common/assert.h"
#include "common/common/logger.h"

namespace Envoy {
namespace Random {

class PsuedoRandomGenerator64 : public RandomGenerator {
public:
  PsuedoRandomGenerator64() = default;
  ~PsuedoRandomGenerator64() override = default;

  void initializeSeed(uint64_t seed) { prng_ = std::make_unique<std::mt19937_64>(seed); }

  // RandomGenerator
  uint64_t random() override {
    // Makes sure initializeSeed() was already called
    ASSERT(prng_ != nullptr);
    uint64_t to_return = (*prng_)();
    ENVOY_LOG_MISC(trace, "random() returned: {}", to_return);
    return to_return;
  }
  std::string uuid() override { return ""; }
  std::unique_ptr<std::mt19937_64> prng_;
};

} // namespace Random

namespace Fuzz {
class ProperSubsetSelector {
public:
  ProperSubsetSelector(const std::string& random_bytestring)
      : random_bytestring_(random_bytestring) {}

  /**
   * This function does proper subset selection on a certain number of elements. It returns a vector
   * of vectors of bytes. Each vector of bytes represents the indexes of a single subset. The
   * "randomness" of the subset that the class will use is determined by a bytestring passed into
   * the class. Example: call into function with a vector {3, 5} representing subset sizes, and 15
   * as number_of_elements. This function would return something such as {{3, 14, 7}, {2, 1, 13, 8,
   * 6}}
   */

  std::vector<std::vector<uint8_t>>
  constructSubsets(const std::vector<uint32_t>& number_of_elements_in_each_subset,
                   uint32_t number_of_elements) {
    std::vector<uint8_t> index_vector;
    index_vector.reserve(number_of_elements);
    for (uint32_t i = 0; i < number_of_elements; i++) {
      index_vector.push_back(i);
    }
    std::vector<std::vector<uint8_t>> subsets;
    subsets.reserve(number_of_elements_in_each_subset.size());
    for (uint32_t i : number_of_elements_in_each_subset) {
      subsets.push_back(constructSubset(i, index_vector));
    }
    return subsets;
  }

private:
  // Builds a single subset by pulling indexes off index_vector_
  std::vector<uint8_t> constructSubset(uint32_t number_of_elements_in_subset,
                                       std::vector<uint8_t>& index_vector) {
    std::vector<uint8_t> subset;
    for (uint32_t i = 0; i < number_of_elements_in_subset && !index_vector.empty(); i++) {
      // Index of bytestring will wrap around if it "overflows" past the random bytestring's length.
      uint64_t index_of_index_vector =
          random_bytestring_[index_of_random_bytestring_ % random_bytestring_.length()] %
          index_vector.size();
      uint64_t index = index_vector.at(index_of_index_vector);
      subset.push_back(index);
      index_vector.erase(index_vector.begin() + index_of_index_vector);
      ++index_of_random_bytestring_;
    }
    return subset;
  }

  // This bytestring will be iterated through representing randomness in order to choose
  // subsets
  const std::string random_bytestring_;
  uint32_t index_of_random_bytestring_ = 0;
};

} // namespace Fuzz
} // namespace Envoy
