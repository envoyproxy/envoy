Fixed a path-segment injection issue in the ``mcp_json_rest_bridge`` filter. Values substituted from
the (untrusted) JSON-RPC ``arguments`` object into the configured upstream REST URL path template
were not percent-encoding ``.`` and ``/``, so a value such as ``../../admin`` could inject additional
path segments into the upstream request path. Path separators in substituted path-template values are
now percent-encoded.
