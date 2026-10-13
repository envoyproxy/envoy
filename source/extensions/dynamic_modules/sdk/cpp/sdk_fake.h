#pragma once

#include <cstddef>
#include <map>
#include <string_view>
#include <vector>

#include "sdk.h"

namespace Envoy {
namespace DynamicModules {

class FakeHeaderMap : public HeaderMap {
public:
  FakeHeaderMap() = default;

  std::vector<std::string_view> get(std::string_view key) const override {
    std::vector<std::string_view> result;
    auto it = headers_.find(std::string(key));
    if (it == headers_.end()) {
      return {};
    }
    if (it->second.empty()) {
      return {};
    }
    for (const auto& value : it->second) {
      result.emplace_back(std::string_view(value));
    }
    return result;
  }

  std::string_view getOne(std::string_view key) const override {
    auto it = headers_.find(std::string(key));
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

  void set(std::string_view key, std::string_view value) override {
    headers_[std::string(key)] = {std::string(value)};
  }

  void add(std::string_view key, std::string_view value) override {
    headers_[std::string(key)].emplace_back(std::string(value));
  }

  void remove(std::string_view key) override { headers_.erase(std::string(key)); }

  void clear() { headers_.clear(); }

private:
  std::map<std::string, std::vector<std::string>> headers_;
};

class FakeBodyBuffer : public BodyBuffer {
public:
  FakeBodyBuffer() = default;

  std::vector<BufferView> getChunks() const override {
    return {BufferView(data_.data(), data_.size())};
  }

  size_t getSize() const override { return data_.size(); }

  void drain(size_t num_bytes) override {
    const size_t to_drain = std::min(num_bytes, getSize());
    data_ = data_.substr(to_drain);
  }

  void append(std::string_view data) override { data_.append(data); }

  void clear() { data_.clear(); }

private:
  std::string data_;
};

} // namespace DynamicModules
} // namespace Envoy
