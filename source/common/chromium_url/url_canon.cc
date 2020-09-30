// Envoy snapshot of Chromium URL path normalization, see README.md.
// NOLINT(namespace-envoy)

// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "common/chromium_url/url_canon.h"

#include "common/chromium_url/envoy_shim.h"

namespace chromium_url {

template class EXPORT_TEMPLATE_DEFINE(COMPONENT_EXPORT(URL)) CanonOutputT<char>;

} // namespace chromium_url
