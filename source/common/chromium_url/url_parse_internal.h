// Envoy snapshot of Chromium URL path normalization, see README.md.
// NOLINT(namespace-envoy)

// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef URL_URL_PARSE_INTERNAL_H_
#define URL_URL_PARSE_INTERNAL_H_

namespace chromium_url {

// We treat slashes and backslashes the same for IE compatibility.
inline bool IsURLSlash(char ch) { return ch == '/' || ch == '\\'; }

} // namespace chromium_url

#endif // URL_URL_PARSE_INTERNAL_H_
