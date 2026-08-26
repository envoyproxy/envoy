Fixed a bug in the ``cache`` and ``cache_v2`` filters where a cacheable response without a ``Date``
header was treated as stale on every lookup. The cache now appends a ``Date`` on insert and on
``304`` as required by https://www.rfc-editor.org/rfc/rfc9110#section-6.6.1 and ages entries stored
without one from their response time as specified in https://www.rfc-editor.org/rfc/rfc9111#section-4.2.1.
