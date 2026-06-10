**Summary of changes**:

* Bug fixes:
  - runtime: fixed RTDS runtime guard override removal so deleting an override restores the process-wide runtime guard value to the default value.

* New features:
  - http2: added opt-in histograms for HTTP/2 header statistics, including header-entry count, header-map byte size, reassembled ``cookie`` header length, and individual ``cookie`` header count. Enable with ``envoy.reloadable_features.http2_record_histograms``; the histograms and runtime guard will be removed in a future Envoy release.
  - http2: added ``envoy.reloadable_features.http2_max_cookies_size_in_kb`` to limit the size of the reassembled ``cookie`` header. By default, no cookie-size limit is enforced.
