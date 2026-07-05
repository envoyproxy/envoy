Added histograms for HTTP/2 header stats, tracking total count of header entries received (including
individual ``cookie`` headers), total byte size of header map entries, total length of the re-assebled ``cookie``
header and total count of individual ``cookie`` headers. Histograms are disabled by default and can be enabled by
setting the runtime guard ``envoy.reloadable_features.http2_record_histograms`` to ``true``.
The histograms and the runtime guard will be removed in a future release of Envoy.
