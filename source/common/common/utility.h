#pragma once

#include <chrono>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "envoy/common/interval_set.h"
#include "envoy/common/time.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/non_copyable.h"

#include "absl/strings/string_view.h"

namespace Envoy {
/**
 * Utility class for formatting dates given an absl::FormatTime style format string.
 */
class DateFormatter {
public:
  DateFormatter(const std::string& format_string) : raw_format_string_(format_string) {
    parse(format_string);
  }

  /**
   * @return std::string representing the GMT/UTC time based on the input time.
   */
  std::string fromTime(const SystemTime& time) const;

  /**
   * @param time_source time keeping source.
   * @return std::string representing the GMT/UTC time of a TimeSource based on the format string.
   */
  std::string now(TimeSource& time_source);

  /**
   * @return std::string the format string used.
   */
  const std::string& formatString() const { return raw_format_string_; }

private:
  void parse(const std::string& format_string);

  using SpecifierOffsets = std::vector<int32_t>;
  std::string fromTimeAndPrepareSpecifierOffsets(time_t time, SpecifierOffsets& specifier_offsets,
                                                 const std::string& seconds_str) const;

  // A container to hold a specifiers (%f, %Nf, %s) found in a format string.
  struct Specifier {
    // To build a subsecond-specifier.
    Specifier(const size_t position, const size_t width, const std::string& segment)
        : position_(position), width_(width), segment_(segment), second_(false) {}

    // To build a second-specifier (%s), the number of characters to be replaced is always 2.
    Specifier(const size_t position, const std::string& segment)
        : position_(position), width_(2), segment_(segment), second_(true) {}

    // The position/index of a specifier in a format string.
    const size_t position_;

    // The width of a specifier, e.g. given %3f, the width is 3. If %f is set as the
    // specifier, the width value should be 9 (the number of nanosecond digits).
    const size_t width_;

    // The string before the current specifier's position and after the previous found specifier. A
    // segment may include absl::FormatTime accepted specifiers. E.g. given
    // "%3f-this-i%s-a-segment-%4f", the current specifier is "%4f" and the segment is
    // "-this-i%s-a-segment-".
    const std::string segment_;

    // As an indication that this specifier is a %s (expect to be replaced by seconds since the
    // epoch).
    const bool second_;
  };

  // This holds all specifiers found in a given format string.
  std::vector<Specifier> specifiers_;

  // This is the format string as supplied in configuration, e.g. "foo %3f bar".
  const std::string raw_format_string_;
};

/**
 * Utility class for access log date/time format with milliseconds support.
 */
class AccessLogDateTimeFormatter {
public:
  static std::string fromTime(const SystemTime& time);
};

/**
 * Real-world time implementation of TimeSource.
 */
class RealTimeSource : public TimeSource {
public:
  // TimeSource
  SystemTime systemTime() override { return std::chrono::system_clock::now(); }
  MonotonicTime monotonicTime() override { return std::chrono::steady_clock::now(); }
};

/**
 * Class used for creating non-copying std::istream's. See InputConstMemoryStream below.
 */
class ConstMemoryStreamBuffer : public std::streambuf {
public:
  ConstMemoryStreamBuffer(const char* data, size_t size);
};

/**
 * std::istream class similar to std::istringstream, except that it provides a view into a region of
 * constant memory. It can be more efficient than std::istringstream because it doesn't copy the
 * provided string.
 *
 * See https://stackoverflow.com/a/13059195/4447365.
 */
class InputConstMemoryStream : public virtual ConstMemoryStreamBuffer, public std::istream {
public:
  InputConstMemoryStream(const char* data, size_t size);
};

/**
 * Utility class for date/time helpers.
 */
class DateUtil {
public:
  /**
   * @return whether a time_point contains a valid, not default constructed time.
   */
  static bool timePointValid(SystemTime time_point);

  /**
   * @return whether a time_point contains a valid, not default constructed time.
   */
  static bool timePointValid(MonotonicTime time_point);

  /**
   * @param time_source time keeping source.
   * @return uint64_t the number of milliseconds since the epoch.
   */
  static uint64_t nowToMilliseconds(TimeSource& time_source);
};

/**
 * Utility routines for working with strings.
 */
class StringUtil {
public:
  /**
   * Callable struct that returns the result of string comparison ignoring case.
   * @param lhs supplies the first string view.
   * @param rhs supplies the second string view.
   * @return true if strings are semantically the same and false otherwise.
   */
  struct CaseInsensitiveCompare {
    // Enable heterogeneous lookup (https://abseil.io/tips/144)
    using is_transparent = void; // NOLINT(readability-identifier-naming)
    bool operator()(absl::string_view lhs, absl::string_view rhs) const;
  };

  /**
   * Callable struct that returns the hash representation of a case-insensitive string_view input.
   * @param key supplies the string view.
   * @return uint64_t hash representation of the supplied string view.
   */
  struct CaseInsensitiveHash {
    // Enable heterogeneous lookup (https://abseil.io/tips/144)
    using is_transparent = void; // NOLINT(readability-identifier-naming)
    uint64_t operator()(absl::string_view key) const;
  };

  /**
   * Definition of unordered set of case-insensitive std::string.
   */
  using CaseUnorderedSet =
      absl::flat_hash_set<std::string, CaseInsensitiveHash, CaseInsensitiveCompare>;

  static const char WhitespaceChars[];

  /**
   * Convert a string to an unsigned long, checking for error.
   * @return pointer to the remainder of 'str' if successful, nullptr otherwise.
   */
  static const char* strtoull(const char* str, uint64_t& out, int base = 10);

  /**
   * Convert a string to an unsigned long, checking for error.
   *
   * Consider absl::SimpleAtoi instead if using base 10.
   *
   * @param return true if successful, false otherwise.
   */
  static bool atoull(const char* str, uint64_t& out, int base = 10);

  /**
   * Convert an unsigned integer to a base 10 string as fast as possible.
   * @param out supplies the string to fill.
   * @param out_len supplies the length of the output buffer. Must be >= MIN_ITOA_OUT_LEN.
   * @param i supplies the number to convert.
   * @return the size of the string, not including the null termination.
   */
  static constexpr size_t MIN_ITOA_OUT_LEN = 21;
  static uint32_t itoa(char* out, size_t out_len, uint64_t i);

  /**
   * Trim leading whitespace from a string view.
   * @param source supplies the string view to be trimmed.
   * @return trimmed string view.
   */
  static absl::string_view ltrim(absl::string_view source);

  /**
   * Trim trailing whitespaces from a string view.
   * @param source supplies the string view to be trimmed.
   * @return trimmed string view.
   */
  static absl::string_view rtrim(absl::string_view source);

  /**
   * Trim leading and trailing whitespaces from a string view.
   * @param source supplies the string view to be trimmed.
   * @return trimmed string view.
   */
  static absl::string_view trim(absl::string_view source);

  /**
   * Removes any specific trailing characters from the end of a string_view.
   *
   * @param source the string_view.
   * @param ch the character to strip from the end of the string_view.
   * @return a view of the string with the end characters removed.
   */
  static absl::string_view removeTrailingCharacters(absl::string_view source, char ch);

  /**
   * Look up for an exactly token in a delimiter-separated string view.
   * @param source supplies the delimiter-separated string view.
   * @param multi-delimiter supplies chars used to split the delimiter-separated string view.
   * @param token supplies the lookup string view.
   * @param trim_whitespace remove leading and trailing whitespaces from each of the split
   * string views; default = true.
   * @return true if found and false otherwise.
   *
   * E.g.,
   *
   * findToken("A=5; b", "=;", "5")   . true
   * findToken("A=5; b", "=;", "A=5") . false
   * findToken("A=5; b", "=;", "A")   . true
   * findToken("A=5; b", "=;", "b")   . true
   * findToken("A=5", ".", "A=5")     . true
   */
  static bool findToken(absl::string_view source, absl::string_view delimiters,
                        absl::string_view token, bool trim_whitespace = true);

  /**
   * Look up for a token in a delimiter-separated string view ignoring case
   * sensitivity.
   * @param source supplies the delimiter-separated string view.
   * @param multi-delimiter supplies chars used to split the delimiter-separated string view.
   * @param token supplies the lookup string view.
   * @param trim_whitespace remove leading and trailing whitespaces from each of the split
   * string views; default = true.
   * @return true if found a string that is semantically the same and false otherwise.
   *
   * E.g.,
   *
   * findToken("hello; world", ";", "HELLO")   . true
   */
  static bool caseFindToken(absl::string_view source, absl::string_view delimiters,
                            absl::string_view key_token, bool trim_whitespace = true);

  /**
   * Crop characters from a string view starting at the first character of the matched
   * delimiter string view until the end of the source string view.
   * @param source supplies the string view to be processed.
   * @param delimiter supplies the string view that delimits the starting point for deletion.
   * @return sub-string of the string view if any.
   *
   * E.g.,
   *
   * cropRight("foo ; ; ; ; ; ; ", ";") == "foo "
   */
  static absl::string_view cropRight(absl::string_view source, absl::string_view delimiters);

  /**
   * Crop characters from a string view starting at the first character of the matched
   * delimiter string view until the beginning of the source string view.
   * @param source supplies the string view to be processed.
   * @param delimiter supplies the string view that delimits the starting point for deletion.
   * @return sub-string of the string view if any.
   *
   * E.g.,
   *
   * cropLeft("foo ; ; ; ; ; ", ";") == " ; ; ; ; "
   */
  static absl::string_view cropLeft(absl::string_view source, absl::string_view delimiters);

  /**
   * Split a delimiter-separated string view.
   * @param source supplies the delimiter-separated string view.
   * @param multi-delimiter supplies chars used to split the delimiter-separated string view.
   * @param keep_empty_string result contains empty strings if the string starts or ends with
   * 'split', or if instances of 'split' are adjacent; default = false.
   * @param trim_whitespace remove leading and trailing whitespaces from each of the split
   * string views; default = false.
   * @return vector containing views of the split strings
   */
  static std::vector<absl::string_view> splitToken(absl::string_view source,
                                                   absl::string_view delimiters,
                                                   bool keep_empty_string = false,
                                                   bool trim_whitespace = false);

  /**
   * Remove tokens from a delimiter-separated string view. The tokens are trimmed before
   * they are compared ignoring case with the elements of 'tokens_to_remove'. The output is
   * built from the trimmed tokens preserving case.
   * @param source supplies the delimiter-separated string view.
   * @param multi-delimiters supplies chars used to split the delimiter-separated string view.
   * @param tokens_to_remove supplies a set of tokens which should not appear in the result.
   * @param joiner contains a string used between tokens in the result.
   * @return string of the remaining joined tokens.
   */
  static std::string removeTokens(absl::string_view source, absl::string_view delimiters,
                                  const CaseUnorderedSet& tokens_to_remove,
                                  absl::string_view joiner);

  /**
   * Size-bounded string copying and concatenation
   */
  static size_t strlcpy(char* dst, const char* src, size_t size);

  /**
   * Version of substr() that operates on a start and end index instead of a start index and a
   * length.
   * @return string substring starting at start, and ending right before end.
   */
  static std::string subspan(absl::string_view source, size_t start, size_t end);

  /**
   * Escape strings for logging purposes. Returns a copy of the string with
   * \n, \r, \t, and " (double quote) escaped.
   * @param source supplies the string to escape.
   * @return escaped string.
   */
  static std::string escape(const std::string& source);

  /**
   * Provide a default value for a string if empty.
   * @param s string.
   * @param default_value replacement for s if empty.
   * @return s is !s.empty() otherwise default_value.
   */
  static const std::string& nonEmptyStringOrDefault(const std::string& s,
                                                    const std::string& default_value);

  /**
   * Convert a string to upper case.
   * @param s string.
   * @return std::string s converted to upper case.
   */
  static std::string toUpper(absl::string_view s);

  /**
   * Removes all the character indices from str contained in the interval-set.
   * @param str the string containing the characters to be removed.
   * @param remove_characters the set of character-intervals.
   * @return std::string the string with the desired characters removed.
   */
  static std::string removeCharacters(const absl::string_view& str,
                                      const IntervalSet<size_t>& remove_characters);
};

/**
 * Utilities for finding primes.
 */
class Primes {
public:
  /**
   * Determines whether x is prime.
   */
  static bool isPrime(uint32_t x);

  /**
   * Finds the next prime number larger than x.
   */
  static uint32_t findPrimeLargerThan(uint32_t x);
};

/**
 * Utilities for working with weighted clusters.
 */
class WeightedClusterUtil {
public:
  /*
   * Returns a WeightedClusterEntry from the given weighted clusters based on
   * the total cluster weight and a random value.
   * @param weighted_clusters a vector of WeightedClusterEntry instances.
   * @param total_cluster_weight the total weight of all clusters.
   * @param random_value the random value.
   * @param ignore_overflow whether to ignore cluster weight overflows.
   * @return a WeightedClusterEntry.
   */
  template <typename WeightedClusterEntry>
  static const WeightedClusterEntry&
  pickCluster(const std::vector<WeightedClusterEntry>& weighted_clusters,
              const uint64_t total_cluster_weight, const uint64_t random_value,
              const bool ignore_overflow) {
    uint64_t selected_value = random_value % total_cluster_weight;
    uint64_t begin = 0;
    uint64_t end = 0;

    // Find the right cluster to route to based on the interval in which
    // the selected value falls. The intervals are determined as
    // [0, cluster1_weight), [cluster1_weight, cluster1_weight+cluster2_weight),..
    for (const WeightedClusterEntry& cluster : weighted_clusters) {
      end = begin + cluster->clusterWeight();
      if (!ignore_overflow) {
        // end > total_cluster_weight: This case can only occur with Runtimes,
        // when the user specifies invalid weights such that
        // sum(weights) > total_cluster_weight.
        ASSERT(end <= total_cluster_weight);
      }

      if (selected_value >= begin && selected_value < end) {
        return cluster;
      }
      begin = end;
    }

    NOT_REACHED_GCOVR_EXCL_LINE;
  }
};

/**
 * Maintains sets of numeric intervals. As new intervals are added, existing ones in the
 * set are combined so that no overlapping intervals remain in the representation.
 *
 * Value can be any type that is comparable with <, ==, and >.
 */
template <typename Value> class IntervalSetImpl : public IntervalSet<Value> {
public:
  // Interval is a pair of Values.
  using Interval = typename IntervalSet<Value>::Interval;

  void insert(Value left, Value right) override {
    if (left == right) {
      return;
    }
    ASSERT(left < right);

    // There 3 cases where we'll decide the [left, right) is disjoint with the
    // current contents, and just need to insert. But we'll structure the code
    // to search for where existing interval(s) needs to be merged, and fall back
    // to the disjoint insertion case.
    if (!intervals_.empty()) {
      const auto left_pos = intervals_.lower_bound(Interval(left, left));
      if (left_pos != intervals_.end() && (right >= left_pos->first)) {
        // upper_bound is exclusive, and we want to be inclusive.
        auto right_pos = intervals_.upper_bound(Interval(right, right));
        if (right_pos != intervals_.begin()) {
          --right_pos;
          if (right_pos->second >= left) {
            // Both bounds overlap, with one or more existing intervals.
            left = std::min(left_pos->first, left);
            right = std::max(right_pos->second, right);
            ++right_pos; // erase is non-inclusive on upper bound.
            intervals_.erase(left_pos, right_pos);
          }
        }
      }
    }
    intervals_.insert(Interval(left, right));
  }

  std::vector<Interval> toVector() const override {
    return std::vector<Interval>(intervals_.begin(), intervals_.end());
  }

  void clear() override { intervals_.clear(); }

private:
  struct Compare {
    bool operator()(const Interval& a, const Interval& b) const { return a.second < b.first; }
  };
  std::set<Interval, Compare> intervals_; // Intervals do not overlap or abut.
};

/**
 * Hashing functor for use with enum class types.
 * This is needed for GCC 5.X; newer versions of GCC, as well as clang7, provide native hashing
 * specializations.
 */
struct EnumClassHash {
  template <typename T> std::size_t operator()(T t) const {
    return std::hash<std::size_t>()(static_cast<std::size_t>(t));
  }
};

/**
 * Computes running standard-deviation using Welford's algorithm:
 * https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Online_algorithm
 */
class WelfordStandardDeviation {
public:
  /**
   * Accumulates a new value into the standard deviation.
   * @param new_value the new value
   */
  void update(double new_value);

  /**
   * @return double the computed mean value.
   */
  double mean() const { return mean_; }

  /**
   * @return uint64_t the number of times update() was called
   */
  uint64_t count() const { return count_; }

  /**
   * @return double the standard deviation.
   */
  double computeStandardDeviation() const;

private:
  double computeVariance() const;

  uint64_t count_{0};
  double mean_{0};
  double m2_{0};
};

template <class Value> struct TrieEntry {
  Value value_{};
  std::array<std::unique_ptr<TrieEntry>, 256> entries_;
};

/**
 * A trie used for faster lookup with lookup time at most equal to the size of the key.
 */
template <class Value> struct TrieLookupTable {

  /**
   * Adds an entry to the Trie at the given Key.
   * @param key the key used to add the entry.
   * @param value the value to be associated with the key.
   * @param overwrite_existing will overwrite the value when the value for a given key already
   * exists.
   * @return false when a value already exists for the given key.
   */
  bool add(absl::string_view key, Value value, bool overwrite_existing = true) {
    TrieEntry<Value>* current = &root_;
    for (uint8_t c : key) {
      if (!current->entries_[c]) {
        current->entries_[c] = std::make_unique<TrieEntry<Value>>();
      }
      current = current->entries_[c].get();
    }
    if (current->value_ && !overwrite_existing) {
      return false;
    }
    current->value_ = value;
    return true;
  }

  /**
   * Finds the entry associated with the key.
   * @param key the key used to find.
   * @return the value associated with the key.
   */
  Value find(absl::string_view key) const {
    const TrieEntry<Value>* current = &root_;
    for (uint8_t c : key) {
      current = current->entries_[c].get();
      if (current == nullptr) {
        return nullptr;
      }
    }
    return current->value_;
  }

  /**
   * Finds the entry associated with the longest prefix. Complexity is O(min(longest key prefix, key
   * length))
   * @param key the key used to find.
   * @return the value matching the longest prefix based on the key.
   */
  Value findLongestPrefix(const char* key) const {
    const TrieEntry<Value>* current = &root_;
    const TrieEntry<Value>* result = nullptr;
    while (uint8_t c = *key) {
      if (current->value_) {
        result = current;
      }

      // https://github.com/facebook/mcrouter/blob/master/mcrouter/lib/fbi/cpp/Trie-inl.h#L126-L143
      current = current->entries_[c].get();
      if (current == nullptr) {
        return result ? result->value_ : nullptr;
      }

      key++;
    }
    return current ? current->value_ : result->value_;
  }

  TrieEntry<Value> root_;
};

// Mix-in class for allocating classes with variable-sized inlined storage.
//
// Use this class by inheriting from it, ensuring that:
//  - The variable sized array is declared as VarType[] as the last
//    member variable of the class.
//  - YourType accurately describes the type that will be stored there,
//    to enable the compiler to perform correct alignment. No casting
//    should be needed.
//  - The class constructor is private, because you need to allocate the
//    class the placed new operator exposed in the protected section below.
//    Constructing the class directly will not provide space for the
//    variable-size data.
//  - You expose a public factory method that return a placement-new, e.g.
//      static YourClass* alloc(size_t num_elements, constructor_args...) {
//        new (num_elements * sizeof(VarType)) YourClass(constructor_args...);
//      }
//
// See InlineString below for an example usage.
//
//
// Perf note: The alignment will be correct and safe without further
// consideration as long as there are no casts. But for micro-optimization,
// consider this case:
//   struct MyStruct : public InlineStorage { uint64_t a_; uint16_t b_; uint8_t data_[]; };
// When compiled with a typical compiler on a 64-bit machine:
//   sizeof(MyStruct) == 16, because the compiler will round up from 10 for uint64_t alignment.
// So:
//   calling new (6) MyStruct() causes an allocation of 16+6=22, rounded up to 24 bytes.
// But data_ doesn't need 8-byte alignment, so it will wind up adjacent to the uint16_t.
//   ((char*) my_struct.data) - ((char*) &my_struct) == 10
// If we had instead declared data_[6], then the whole allocation would have fit in 16 bytes.
// Instead:
//   - the starting address of data will not be 8-byte aligned. This is not required
//     by the C++ standard for a uint8_t, but may be suboptimal on some processors.
//   - the 6 bytes of data will be at byte offsets 10 to 15, and bytes 16 to 23 will be
//     unused. This may be surprising to some users, and suboptimal in resource usage.
// One possible tweak is to declare data_ as a uint64_t[], or to use an `alignas`
// declaration. As always, micro-optimizations should be informed by
// microbenchmarks, showing the benefit.
class InlineStorage : public NonCopyable {
public:
  // Custom delete operator to keep C++14 from using the global operator delete(void*, size_t),
  // which would result in the compiler error:
  // "exception cleanup for this placement new selects non-placement operator delete"
  static void operator delete(void* address) { ::operator delete(address); }

protected:
  /**
   * @param object_size the size of the base object; supplied automatically by the compiler.
   * @param data_size the amount of variable-size storage to be added, in bytes.
   * @return a variable-size object based on data_size_bytes.
   */
  static void* operator new(size_t object_size, size_t data_size_bytes) {
    return ::operator new(object_size + data_size_bytes);
  }
};

class InlineString;
using InlineStringPtr = std::unique_ptr<InlineString>;

// Represents immutable string data, keeping the storage inline with the
// object. These cannot be copied or held by value; they must be created
// as unique pointers.
//
// Note: this is not yet proven better (smaller or faster) than std::string for
// all applications, but memory-size improvements have been measured for one
// application (Stats::SymbolTableImpl). This is presented here to serve as an
// example of how to use InlineStorage.
class InlineString : public InlineStorage {
public:
  /**
   * @param str the string_view for which to create an InlineString
   * @return a unique_ptr to the InlineString containing the bytes of str.
   */
  static InlineStringPtr create(absl::string_view str) {
    return InlineStringPtr(new (str.size()) InlineString(str.data(), str.size()));
  }

  /**
   * @return a std::string copy of the InlineString.
   */
  std::string toString() const { return std::string(data_, size_); }

  /**
   * @return a string_view into the InlineString.
   */
  absl::string_view toStringView() const { return {data_, size_}; }

  /**
   * @return the number of bytes in the string
   */
  size_t size() const { return size_; }

  /**
   * @return a pointer to the first byte of the string.
   */
  const char* data() const { return data_; }

private:
  // Constructor is declared private so that no one constructs one without the
  // proper size allocation. to accommodate the variable-size buffer.
  InlineString(const char* str, size_t size);

  uint32_t size_;
  char data_[];
};

} // namespace Envoy
