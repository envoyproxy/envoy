.. _faq_deprecation:

How are configuration deprecations handled?
===========================================

As documented in the "Breaking change policy" in :repo:`CONTRIBUTING.md`, features can be marked
as deprecated at any point as long as there is a replacement available. Each deprecation is
annotated in the API proto itself and explained in detail in the
:ref:`Envoy documentation <deprecated>`.

For the first 3 months following deprecation, use of deprecated fields will result in a logged
warning and incrementing the :ref:`deprecated_feature_use <runtime_stats>` counter.
After that point, the field will be annotated as fatal-by-default and further use of the field
will be treated as invalid configuration unless
:ref:`runtime overrides <config_runtime_deprecation>` are employed to re-enable use.
