.. _config_http_filters_ip_tagging:

Ip tagging filter
====================

This is a filter which enables Envoy to tag requests with extra information such as location, cloud source and any
extra data. This is useful to prevent against DDoS.

**Note**: this filter is under active development, and currently does not perform any tagging on requests. In other
words, installing this filter is a nop in the filter chain.

.. code-block:: json

  {
    "type": "decoder",
    "name": "ip_tagging",
    "config": {
      "request_type": "...",
      "ip_tags": []
    }
  }

request_type:
  (optional, string) The type of requests the filter should apply to. ``external``, ``internal``, or ``both``.
  Defaults to ``both``.

ip_tags:
  (optional, array) Specifies the list of ip tags to set for a request.

Ip tags
-------
.. code-block:: json

  {
    "ip_tag_name": "...",
    "ip_list": []
  }

ip_tag_name:
  (required, string) Specifies the ip tag name to apply. The names will be added to the request's ``x-envoy-ip-tag``
  header if the request's XFF address belongs to a range present in the ``ip_list``

ip_list:
  (required, list of strings) A list of IP address and subnet masks that will be tagged with the ``ip_tag_name``. Both
  IPv4, and IPv6 CIDR addresses are allowed here.