Fixed a path-segment injection issue in the ``mcp_json_rest_bridge`` filter. Values substituted into
a *simple* path-template variable (e.g. ``{id}``) were not percent-encoding ``/`` or ``.``, so an
attacker-supplied value such as ``../../admin`` could inject additional path segments or ``..``
traversal into the upstream request path. Simple template variables now percent-encode ``/`` and
``.`` to confine the value to a single segment; wildcard variables (e.g. ``{name=projects/*}``),
whose values are intentionally multi-segment resource names, are unchanged.
