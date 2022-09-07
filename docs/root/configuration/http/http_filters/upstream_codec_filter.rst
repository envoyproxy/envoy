.. _config_http_filters_upstream_codec:

Upstream Codec
==============

The upstream codec filter is the only supported terminal filter for upstream filters.
It is responsible for encoding headers/body/data to the upstream codec, and decoding
headers/body/data from the upstream codec, as well as updating stats and timing metrics.

There currently are no supported configuration options for the upstream codec filter.
