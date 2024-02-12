#pragma once

// Copyright The OpenTelemetry Authors
// SPDX-License-Identifier: Apache-2.0
//
// Originally taken from the official OpenTelemetry C++ API/SDK
// https://github.com/open-telemetry/opentelemetry-cpp/blob/v1.13.0/api/include/opentelemetry/trace/trace_state.h

#include <cstdint>
#include <memory>
#include <string>

#include "source/extensions/tracers/opentelemetry/kv_properties.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

/**
 * TraceState carries tracing-system specific context in a list of key-value pairs. TraceState
 * allows different vendors to propagate additional information and inter-operate with their legacy
 * id formats.
 *
 * For more information, see the W3C Trace Context specification:
 * https://www.w3.org/TR/trace-context
 */
class TraceState {
public:
  static constexpr int kKeyMaxSize = 256;
  static constexpr int kValueMaxSize = 256;
  static constexpr int kMaxKeyValuePairs = 32;
  static constexpr auto kKeyValueSeparator = '=';
  static constexpr auto kMembersSeparator = ',';

  static std::shared_ptr<TraceState> getDefault() {
    static std::shared_ptr<TraceState> ts{new TraceState()};
    return ts;
  }

  /**
   * Returns shared_ptr to a newly created TraceState parsed from the header provided.
   * @param header Encoding of the tracestate header defined by
   * the W3C Trace Context specification https://www.w3.org/TR/trace-context/
   * @return TraceState A new TraceState instance or DEFAULT
   */
  static std::shared_ptr<TraceState> fromHeader(absl::string_view header) noexcept {

    KeyValueStringTokenizer kv_str_tokenizer(header);
    size_t cnt = kv_str_tokenizer.numTokens(); // upper bound on number of kv pairs
    if (cnt > kMaxKeyValuePairs) {
      cnt = kMaxKeyValuePairs;
    }

    std::shared_ptr<TraceState> ts(new TraceState(cnt));
    bool kv_valid;
    absl::string_view key, value;
    while (kv_str_tokenizer.next(kv_valid, key, value) && ts->kv_properties_->size() < cnt) {
      if (kv_valid == false) {
        return getDefault();
      }

      if (!isValidKey(key) || !isValidValue(value)) {
        // invalid header. return empty TraceState
        ts->kv_properties_ = std::make_unique<KeyValueProperties>();
        break;
      }

      ts->kv_properties_->addEntry(key, value);
    }

    return ts;
  }

  /**
   * Creates a w3c tracestate header from TraceState object
   */
  std::string toHeader() const noexcept {
    std::string header_s;
    bool first = true;
    kv_properties_->getAllEntries(
        [&header_s, &first](absl::string_view key, absl::string_view value) noexcept {
          if (!first) {
            header_s.append(",");
          } else {
            first = false;
          }
          header_s.append(std::string(key.data(), key.size()));
          header_s.append(1, kKeyValueSeparator);
          header_s.append(std::string(value.data(), value.size()));
          return true;
        });
    return header_s;
  }

  /**
   *  Returns `value` associated with `key` passed as argument
   *  Returns empty string if key is invalid  or not found
   */
  bool get(absl::string_view key, std::string& value) const noexcept {
    if (!isValidKey(key)) {
      return false;
    }

    return kv_properties_->getValue(key, value);
  }

  /**
   * Returns shared_ptr of `new` TraceState object with following mutations applied to the existing
   * instance: Update Key value: The updated value must be moved to beginning of List Add : The new
   * key-value pair SHOULD be added to beginning of List
   *
   * If the provided key-value pair is invalid, or results in trace state that violates the
   * trace context specification, empty TraceState instance will be returned.
   *
   * If the existing object has maximum list members, it's copy is returned.
   */
  std::shared_ptr<TraceState> set(const absl::string_view& key,
                                  const absl::string_view& value) noexcept {
    auto curr_size = kv_properties_->size();
    if (!isValidKey(key) || !isValidValue(value)) {
      // max size reached or invalid key/value. Returning empty TraceState
      return TraceState::getDefault();
    }
    auto allocate_size = curr_size;
    if (curr_size < kMaxKeyValuePairs) {
      allocate_size += 1;
    }
    std::shared_ptr<TraceState> ts(new TraceState(allocate_size));
    if (curr_size < kMaxKeyValuePairs) {
      // add new field first
      ts->kv_properties_->addEntry(key, value);
    }
    // add rest of the fields.
    kv_properties_->getAllEntries([&ts](absl::string_view key, absl::string_view value) {
      ts->kv_properties_->addEntry(key, value);
      return true;
    });
    return ts;
  }

  /**
   * Returns shared_ptr to a `new` TraceState object after removing the attribute with given key (
   * if present )
   * @returns empty TraceState object if key is invalid
   * @returns copy of original TraceState object if key is not present (??)
   */
  std::shared_ptr<TraceState> remove(const absl::string_view& key) noexcept {
    if (!isValidKey(key)) {
      return TraceState::getDefault();
    }
    auto curr_size = kv_properties_->size();
    auto allocate_size = curr_size;
    std::string unused;
    if (kv_properties_->getValue(key, unused)) {
      allocate_size -= 1;
    }
    std::shared_ptr<TraceState> ts(new TraceState(allocate_size));
    kv_properties_->getAllEntries([&ts, &key](absl::string_view e_key, absl::string_view e_value) {
      if (key != e_key) {
        ts->kv_properties_->addEntry(e_key, e_value);
      }
      return true;
    });
    return ts;
  }

  // Returns true if there are no keys, false otherwise.
  bool empty() const noexcept { return kv_properties_->size() == 0; }

  // @return all key-values entries by repeatedly invoking the function reference passed as argument
  // for each entry
  bool
  getAllEntries(std::function<bool(absl::string_view, absl::string_view)> callback) const noexcept {
    return kv_properties_->getAllEntries(callback);
  }

  /** Returns whether key is a valid key. See https://www.w3.org/TR/trace-context/#key
   * Identifiers MUST begin with a lowercase letter or a digit, and can only contain
   * lowercase letters (a-z), digits (0-9), underscores (_), dashes (-), asterisks (*),
   * and forward slashes (/).
   * For multi-tenant vendor scenarios, an at sign (@) can be used to prefix the vendor name.
   *
   */
  static bool isValidKey(absl::string_view key) {
    if (key.empty() || key.size() > kKeyMaxSize || !isLowerCaseAlphaOrDigit(key[0])) {
      return false;
    }

    int ats = 0;

    for (const char c : key) {
      if (!isLowerCaseAlphaOrDigit(c) && c != '_' && c != '-' && c != '@' && c != '*' && c != '/') {
        return false;
      }
      if ((c == '@') && (++ats > 1)) {
        return false;
      }
    }
    return true;
  }

  /** Returns whether value is a valid value. See https://www.w3.org/TR/trace-context/#value
   * The value is an opaque string containing up to 256 printable ASCII (RFC0020)
   *  characters ((i.e., the range 0x20 to 0x7E) except comma , and equal =)
   */
  static bool isValidValue(absl::string_view value) {
    if (value.empty() || value.size() > kValueMaxSize) {
      return false;
    }

    for (const char c : value) {
      if (c < ' ' || c > '~' || c == ',' || c == '=') {
        return false;
      }
    }
    return true;
  }

private:
  TraceState() : kv_properties_(new KeyValueProperties()) {}
  TraceState(size_t size) : kv_properties_(new KeyValueProperties(size)) {}
  static bool isLowerCaseAlphaOrDigit(char c) { return isdigit(c) || islower(c); }

private:
  // Store entries in a C-style array to avoid using std::array or std::vector.
  std::unique_ptr<KeyValueProperties> kv_properties_;
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
