#include <random>
#include <vector>

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
    uint64_t to_return = (*prng_.get())();
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
    index_vector_.clear();
    index_vector_.reserve(number_of_elements);
    for (uint32_t i = 0; i < number_of_elements; i++) {
      index_vector_.push_back(i);
    }
    std::vector<std::vector<uint8_t>> subsets;
    for (uint32_t i = 0; i < number_of_elements_in_each_subset.size(); i++) {
      subsets.push_back(constructSubset(number_of_elements_in_each_subset.at(i)));
    }
    return subsets;
  }

private:
  // Builds a single subset by pulling indexes off index_vector_
  std::vector<uint8_t> constructSubset(uint32_t number_of_elements_in_subset) {
    std::vector<uint8_t> subset;
    for (uint32_t i = 0; i < number_of_elements_in_subset && index_vector_.size() != 0; i++) {
      // Index of bytestring will wrap around if it "overflows" past the random bytestring's length.
      uint64_t index_of_index_vector =
          random_bytestring_[index_of_random_bytestring_ % random_bytestring_.length()] %
          index_vector_.size();
      uint64_t index = index_vector_.at(index_of_index_vector);
      subset.push_back(index);
      index_vector_.erase(index_vector_.begin() + index_of_index_vector);
      ++index_of_random_bytestring_;
    }
    return subset;
  }

  std::vector<uint8_t> index_vector_;

  // This bytestring will be iterated through logically representing randomness in order to choose
  // subsets
  std::string random_bytestring_;
  uint32_t index_of_random_bytestring_ = 0;
};

} // namespace Fuzz
} // namespace Envoy
