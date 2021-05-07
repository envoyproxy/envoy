#pragma once

#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace Envoy {
namespace Platform {

using RawHeaderMap = absl::flat_hash_map<std::string, std::vector<std::string>>;

class Headers {
public:
  class const_iterator {
  public:
    const_iterator(RawHeaderMap::const_iterator position) : position_(position){};

    using iterator_category = RawHeaderMap::const_iterator::iterator_category;
    using value_type = std::string;
    using reference = const value_type&;
    using pointer = const value_type*;
    using difference_type = std::ptrdiff_t;

    reference operator*() const { return position_->first; }
    pointer operator->() { return &position_->first; }

    const_iterator& operator++() {
      this->position_++;
      return *this;
    }
    const_iterator operator++(int) {
      auto tmp = *this;
      ++(*this);
      return tmp;
    }

    friend bool operator==(const const_iterator& a, const const_iterator& b) {
      return a.position_ == b.position_;
    }
    friend bool operator!=(const const_iterator& a, const const_iterator& b) {
      return a.position_ != b.position_;
    }

  private:
    RawHeaderMap::const_iterator position_;
  };

  virtual ~Headers() {}

  const_iterator begin() const;
  const_iterator end() const;

  const std::vector<std::string>& operator[](const std::string& key) const;
  const RawHeaderMap& allHeaders() const;
  bool contains(const std::string& key) const;

protected:
  Headers(const RawHeaderMap& headers);

private:
  RawHeaderMap headers_;
};

} // namespace Platform
} // namespace Envoy
