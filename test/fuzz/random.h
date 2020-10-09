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
  ProperSubsetSelector(std::string random_bytestring) {
    random_bytestring_ = random_bytestring;
    index_of_random_bytestring_ = 0;
  }

  // Returns a vector of vectors, each vector of bytes representing a single subset
  std::vector<std::vector<uint8_t>>
  constructSubsets(uint32_t number_of_subsets,
                   std::vector<uint32_t> number_of_elements_in_each_subset,
                   uint32_t number_of_elements) {
    ASSERT(number_of_subsets == number_of_elements_in_each_subset.size());
    index_vector_.clear();
    index_vector_.reserve(number_of_elements);
    for (uint32_t i = 0; i < number_of_elements; i++) {
      index_vector_.push_back(i);
    }
    std::vector<std::vector<uint8_t>> subsets;
    for (uint32_t i = 0; i < number_of_subsets; i++) {
      subsets.push_back(constructSubset(number_of_elements_in_each_subset.at(i)));
    }
    return subsets;
  }

  // Builds a single subset by pulling indexes off index_vector_
  std::vector<uint8_t> constructSubset(uint32_t number_of_elements_in_subset) {
    std::vector<uint8_t> subset;
    for (uint32_t i = 0; i < number_of_elements_in_subset && index_vector_.size() != 0; i++) {
      uint64_t index_of_index_vector =
          random_bytestring_[index_of_random_bytestring_] % index_vector_.size();
      uint64_t index = index_vector_.at(index_of_index_vector);
      subset.push_back(index);
      index_vector_.erase(index_vector_.begin() + index_of_index_vector);
      ++index_of_random_bytestring_;
      // Index of bytestring will wrap around if overflows
      if (index_of_random_bytestring_ == random_bytestring_.length()) {
        index_of_random_bytestring_ = 0;
      }
    }
    return subset;
  }

  std::vector<uint8_t> index_vector_;

  // This bytestring will be iterated through logically representing randomness in order to choose
  // subsets
  std::string random_bytestring_;
  uint32_t index_of_random_bytestring_;
};

} // namespace Fuzz
} // namespace Envoy
