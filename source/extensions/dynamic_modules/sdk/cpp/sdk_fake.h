#pragma once

#include <cstddef>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "sdk.h"

namespace Envoy {
namespace DynamicModules {

class FakeHeaderMap : public HeaderMap {
public:
  FakeHeaderMap() = default;

  std::vector<absl::string_view> get(absl::string_view key) const override {
    std::vector<absl::string_view> result;
    auto it = headers_.find(key);
    if (it == headers_.end()) {
      return {};
    }
    if (it->second.empty()) {
      return {};
    }
    for (const auto& value : it->second) {
      result.emplace_back(absl::string_view(value));
    }
    return result;
  }

  absl::string_view getOne(absl::string_view key) const override {
    auto it = headers_.find(key);
    if (it == headers_.end()) {
      return {};
    }
    if (it->second.empty()) {
      return {};
    }
    return it->second[0];
  }

  std::vector<HeaderView> getAll() const override {
    std::vector<HeaderView> result;
    result.reserve(headers_.size());
    for (const auto& [key, values] : headers_) {
      for (const auto& value : values) {
        result.emplace_back(key.data(), key.size(), value.data(), value.size());
      }
    }
    return result;
  }

  size_t size() const override {
    size_t count = 0;
    for (const auto& [key, values] : headers_) {
      count += values.size();
    }
    return count;
  }

  void set(absl::string_view key, absl::string_view value) override {
    headers_[key] = {std::string(value)};
  }

  void add(absl::string_view key, absl::string_view value) override {
    headers_[key].emplace_back(value);
  }

  void remove(absl::string_view key) override { headers_.erase(key); }

  void clear() { headers_.clear(); }

private:
  absl::flat_hash_map<std::string, std::vector<std::string>> headers_;
};

class FakeBodyBuffer : public BodyBuffer {
public:
  FakeBodyBuffer() = default;

  std::vector<BufferView> getChunks() override { return {BufferView(data_.data(), data_.size())}; }

  size_t getSize() override { return data_.size(); }

  void drain(size_t num_bytes) override {
    const size_t to_drain = std::min(num_bytes, getSize());
    data_ = data_.substr(to_drain);
  }

  void append(absl::string_view data) override { data_.append(data); }

  void clear() { data_.clear(); }

private:
  std::string data_;
};

} // namespace DynamicModules
} // namespace Envoy
