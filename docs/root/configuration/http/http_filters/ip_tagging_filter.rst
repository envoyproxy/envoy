.. _config_http_filters_ip_tagging:

IP Tagging
==========

The HTTP IP Tagging filter sets the *x-envoy-ip-tags* header or the provided :ref: `ip_tag_header <envoy_v3_api_field_extensions.filters.http.ip_tagging.v3.IPTagging.ip_tag_header>`
with the string tags for the trusted address from :ref:`x-forwarded-for <config_http_conn_man_headers_x-forwarded-for>`.

If the :ref: `ip_tag_header.action <envoy_v3_api_field_extensions.filters.http.ip_tagging.v3.IPTagging.ip_tag_header.action>`
is set to *SANITIZE* (the default), the header mentioned in :ref: `ip_tag_header.header <envoy_v3_api_field_extensions.filters.http.ip_tagging.v3.IPTagging.ip_tag_header.header>`
will be replaced with the new tags, and clearing it if there are no tags.
If it is instead set to *APPEND_IF_EXISTS_OR_ADD*, the header will only be appended to, retaining any existing values.

Due to backward compatibility, if the :ref: `ip_tag_header <envoy_v3_api_field_extensions.filters.http.ip_tagging.v3.IPTagging.ip_tag_header>`
is empty, the tags will be appended to the *x-envoy-ip-tags* header.
This header is cleared at the start of the filter chain, so this is in effect the same as sanitize.
When applying this filter multiple times within the same filter chain, this retains the old behaviour which combines the tags from each invocation.

The implementation for IP Tagging provides a scalable way to compare an IP address to a large list of CIDR
ranges efficiently. The underlying algorithm for storing tags and IP address subnets is a Level-Compressed trie
described in the paper `IP-address lookup using
LC-tries <https://www.csc.kth.se/~snilsson/publications/IP-address-lookup-using-LC-tries/text.pdf>`_ by S. Nilsson and
G. Karlsson.


Configuration
-------------
* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.ip_tagging.v3.IPTagging``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.ip_tagging.v3.IPTagging>`

Statistics
----------

The IP Tagging filter outputs statistics in the ``http.<stat_prefix>.ip_tagging.`` namespace. The stat prefix comes from
the owning HTTP connection manager.

.. csv-table::
  :header: Name, Type, Description
  :widths: 1, 1, 2

        <tag_name>.hit, Counter, Total number of requests that have the ``<tag_name>`` applied to it
        no_hit, Counter, Total number of requests with no applicable IP tags
        total, Counter, Total number of requests the IP Tagging Filter operated on

Runtime
-------

The IP Tagging filter supports the following runtime settings:

ip_tagging.http_filter_enabled
    The % of requests for which the filter is enabled. Default is 100.
