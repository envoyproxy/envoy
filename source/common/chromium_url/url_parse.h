// Envoy snapshot of Chromium URL path normalization, see README.md.
// NOLINT(namespace-envoy)

// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_PARSE_H_
#define URL_PARSE_H_

namespace chromium_url {

// Component ------------------------------------------------------------------

// Represents a substring for URL parsing.
struct Component {
  Component() : begin(0), len(-1) {}

  // Normal constructor: takes an offset and a length.
  Component(int b, int l) : begin(b), len(l) {}

  int end() const { return begin + len; }

  // Returns true if this component is valid, meaning the length is given. Even
  // valid components may be empty to record the fact that they exist.
  bool is_valid() const { return (len != -1); }

  // Returns true if the given component is specified on false, the component
  // is either empty or invalid.
  bool is_nonempty() const { return (len > 0); }

  void reset() {
    begin = 0;
    len = -1;
  }

  bool operator==(const Component& other) const { return begin == other.begin && len == other.len; }

  int begin; // Byte offset in the string of this component.
  int len;   // Will be -1 if the component is unspecified.
};

// Helper that returns a component created with the given begin and ending
// points. The ending point is non-inclusive.
inline Component MakeRange(int begin, int end) { return Component(begin, end - begin); }

} // namespace chromium_url

#endif // URL_PARSE_H_
