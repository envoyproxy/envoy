Fixed a path-segment injection issue in the ``mcp_json_rest_bridge`` filter. A value substituted
into a *simple* path-template variable (e.g. ``{id}``) was percent-encoded with a set that left
``/`` and ``.`` unescaped, so an attacker-supplied value such as ``../../admin`` could inject
additional path segments or ``..`` traversal into the (non-re-normalized) upstream request path.
Simple template variables now reject a value that spans multiple path segments or is a ``.``/``..``
traversal segment; wildcard variables (e.g. ``{name=projects/*}``), whose values are intentionally
multi-segment resource names, are unchanged.
