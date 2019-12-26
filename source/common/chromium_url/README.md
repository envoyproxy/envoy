This is a manually minified variant of
https://chromium.googlesource.com/chromium/src.git/+archive/74.0.3729.15/url.tar.gz,
providing just the parts needed for `url::CanonicalizePath()`. This is intended
to support a security release fix for CVE-2019-9901. Long term we need this to
be moved to absl or QUICHE for upgrades and long-term support.

Some specific transforms of interest:
* The namespace `url` was changed to `chromium_url`.
* `url_parse.h` is minified to just `Component` and flattened back into the URL
  directory. It does not contain any non-Chromium authored code any longer and
  so does not have a separate LICENSE.
* `envoy_shim.h` adapts various macros to the Envoy context.
* Anything not reachable from `url::CanonicalizePath()` has been dropped.
* Header include paths have changed as needed.
* BUILD was manually written.
* Various clang-tidy and format fixes.
