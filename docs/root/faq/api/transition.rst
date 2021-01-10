.. _faq_api_version_transition:

How do I continue to use the v2 xDS API until Q1 2021?
======================================================

The v2 xDS API is deprecated and disabled-by-default in Envoy. Envoy support for v2 xDS will be
removed during Q1 2021.

In the interim, you can continue to use the v2 API for the this transitional period by:

* Setting :option:`--bootstrap-version` 2 on the CLI for a v2 bootstrap file.
* Enabling the runtime `envoy.reloadable_features.enable_deprecated_v2_api` feature. This is
  implicitly enabled if a v2 :option:`--bootstrap-version` is set.

See this :ref:`entry <faq_api_v3_config>` for guidance on how to configure the v3 API.
