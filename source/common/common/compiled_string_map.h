#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include "absl/strings/string_view.h"

namespace Envoy {

template <class Value> class CompiledStringMap {
  using FindFn = std::function<Value(const absl::string_view&)>;

public:
  using Pair = std::pair<std::string, Value>;
  Value find(const absl::string_view& key) const {
    if (key.size() >= table_.size() || table_[key.size()] == nullptr) {
      return {};
    }
    return table_[key.size()](key);
  };
  void compile(std::vector<Pair> initial) {
    if (initial.empty()) {
      return;
    }
    size_t longest = 0;
    for (const Pair& pair : initial) {
      longest = std::max(pair.first.size(), longest);
    }
    table_.resize(longest + 1);
    std::sort(initial.begin(), initial.end(),
              [](const Pair& a, const Pair& b) { return a.first.size() < b.first.size(); });
    auto it = initial.begin();
    for (size_t i = 0; i <= longest; i++) {
      auto start = it;
      while (it != initial.end() && it->first.size() == i) {
        it++;
      }
      if (it != start) {
        std::vector<Pair> node_contents;
        node_contents.reserve(it - start);
        std::copy(start, it, std::back_inserter(node_contents));
        table_[i] = createEqualLengthNode(node_contents);
      }
    }
  }

private:
  static FindFn createEqualLengthNode(std::vector<Pair> node_contents) {
    if (node_contents.size() == 1) {
      return [pair = node_contents[0]](const absl::string_view& key) -> Value {
        if (key != pair.first) {
          return {};
        }
        return pair.second;
      };
    }
    struct IndexSplitInfo {
      uint8_t index, min, max, count;
    } best{0, 0, 0, 0};
    for (size_t i = 0; i < node_contents[0].first.size(); i++) {
      std::array<bool, 256> hits{};
      IndexSplitInfo info{static_cast<uint8_t>(i), 255, 0, 0};
      for (size_t j = 0; j < node_contents.size(); j++) {
        uint8_t v = node_contents[j].first[i];
        if (!hits[v]) {
          hits[v] = true;
          info.count++;
          info.min = std::min(v, info.min);
          info.max = std::max(v, info.max);
        }
      }
      if (info.count > best.count) {
        best = info;
      }
    }
    std::vector<FindFn> nodes;
    nodes.resize(best.max - best.min + 1);
    std::sort(node_contents.begin(), node_contents.end(), [&best](const Pair& a, const Pair& b) {
      return a.first[best.index] < b.first[best.index];
    });
    auto it = node_contents.begin();
    for (int i = best.min; i <= best.max; i++) {
      auto start = it;
      while (it != node_contents.end() && it->first[best.index] == i) {
        it++;
      }
      if (it != start) {
        std::vector<Pair> next_contents;
        next_contents.reserve(it - start);
        std::copy(start, it, std::back_inserter(next_contents));
        nodes[i - best.min] = createEqualLengthNode(next_contents);
      }
    }
    return [nodes = std::move(nodes), min = best.min,
            index = best.index](const absl::string_view& key) -> Value {
      uint8_t k = static_cast<uint8_t>(key[index]);
      if (k < min || k >= min + nodes.size() || nodes[k - min] == nullptr) {
        return {};
      }
      return nodes[k - min](key);
    };
  }
  static Value findEmpty(const absl::string_view&) { return nullptr; }
  std::vector<FindFn> table_;
};

} // namespace Envoy
