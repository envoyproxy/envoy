// Envoy snapshot of Chromium URL path normalization, see README.md.
// NOLINT(namespace-envoy)

// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common/chromium_url/url_canon_internal.h"

namespace chromium_url {

// See the header file for this array's declaration.
const unsigned char kSharedCharTypeTable[0x100] = {
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0x00 - 0x0f
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,                                           // 0x10 - 0x1f
    0,                                           // 0x20  ' ' (escape spaces in queries)
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT, // 0x21  !
    0,                                           // 0x22  "
    0,                          // 0x23  #  (invalid in query since it marks the ref)
    CHAR_QUERY | CHAR_USERINFO, // 0x24  $
    CHAR_QUERY | CHAR_USERINFO, // 0x25  %
    CHAR_QUERY | CHAR_USERINFO, // 0x26  &
    0,                          // 0x27  '  (Try to prevent XSS.)
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,             // 0x28  (
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,             // 0x29  )
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,             // 0x2a  *
    CHAR_QUERY | CHAR_USERINFO,                              // 0x2b  +
    CHAR_QUERY | CHAR_USERINFO,                              // 0x2c  ,
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,             // 0x2d  -
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_COMPONENT, // 0x2e  .
    CHAR_QUERY,                                              // 0x2f  /
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_DEC | CHAR_OCT |
        CHAR_COMPONENT, // 0x30  0
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_DEC | CHAR_OCT |
        CHAR_COMPONENT, // 0x31  1
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_DEC | CHAR_OCT |
        CHAR_COMPONENT, // 0x32  2
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_DEC | CHAR_OCT |
        CHAR_COMPONENT, // 0x33  3
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_DEC | CHAR_OCT |
        CHAR_COMPONENT, // 0x34  4
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_DEC | CHAR_OCT |
        CHAR_COMPONENT, // 0x35  5
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_DEC | CHAR_OCT |
        CHAR_COMPONENT, // 0x36  6
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_DEC | CHAR_OCT |
        CHAR_COMPONENT,                                                            // 0x37  7
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_DEC | CHAR_COMPONENT, // 0x38  8
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_DEC | CHAR_COMPONENT, // 0x39  9
    CHAR_QUERY,                                                                    // 0x3a  :
    CHAR_QUERY,                                                                    // 0x3b  ;
    0,          // 0x3c  <  (Try to prevent certain types of XSS.)
    CHAR_QUERY, // 0x3d  =
    0,          // 0x3e  >  (Try to prevent certain types of XSS.)
    CHAR_QUERY, // 0x3f  ?
    CHAR_QUERY, // 0x40  @
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x41  A
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x42  B
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x43  C
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x44  D
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x45  E
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x46  F
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x47  G
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x48  H
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x49  I
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x4a  J
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x4b  K
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x4c  L
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x4d  M
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x4e  N
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x4f  O
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x50  P
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x51  Q
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x52  R
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x53  S
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x54  T
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x55  U
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x56  V
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x57  W
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_COMPONENT,            // 0x58  X
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x59  Y
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x5a  Z
    CHAR_QUERY,                                                         // 0x5b  [
    CHAR_QUERY,                                                         // 0x5c  '\'
    CHAR_QUERY,                                                         // 0x5d  ]
    CHAR_QUERY,                                                         // 0x5e  ^
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x5f  _
    CHAR_QUERY,                                                         // 0x60  `
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x61  a
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x62  b
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x63  c
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x64  d
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x65  e
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_HEX | CHAR_COMPONENT, // 0x66  f
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x67  g
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x68  h
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x69  i
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x6a  j
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x6b  k
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x6c  l
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x6d  m
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x6e  n
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x6f  o
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x70  p
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x71  q
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x72  r
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x73  s
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x74  t
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x75  u
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x76  v
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x77  w
    CHAR_QUERY | CHAR_USERINFO | CHAR_IPV4 | CHAR_COMPONENT,            // 0x78  x
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x79  y
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x7a  z
    CHAR_QUERY,                                                         // 0x7b  {
    CHAR_QUERY,                                                         // 0x7c  |
    CHAR_QUERY,                                                         // 0x7d  }
    CHAR_QUERY | CHAR_USERINFO | CHAR_COMPONENT,                        // 0x7e  ~
    0,                                                                  // 0x7f
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0x80 - 0x8f
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0x90 - 0x9f
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0xa0 - 0xaf
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0xb0 - 0xbf
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0xc0 - 0xcf
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0xd0 - 0xdf
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0xe0 - 0xef
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0, // 0xf0 - 0xff
};

const char kHexCharLookup[0x10] = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
};

const char kCharToHexLookup[8] = {
    0,        // 0x00 - 0x1f
    '0',      // 0x20 - 0x3f: digits 0 - 9 are 0x30 - 0x39
    'A' - 10, // 0x40 - 0x5f: letters A - F are 0x41 - 0x46
    'a' - 10, // 0x60 - 0x7f: letters a - f are 0x61 - 0x66
    0,        // 0x80 - 0x9F
    0,        // 0xA0 - 0xBF
    0,        // 0xC0 - 0xDF
    0,        // 0xE0 - 0xFF
};

} // namespace chromium_url
