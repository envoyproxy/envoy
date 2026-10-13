
.. _config_http_filters_checksum:

Checksum
========

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.checksum.v3alpha.ChecksumConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.checksum.v3alpha.ChecksumConfig>`

.. attention::

   The checksum filter is only included in :ref:`contrib images <install_contrib>`

.. attention::

   The checksum filter is experimental and is currently under active development.

The checksum filter matches the hashed body of a response from an upstream download path against an expected sha256 hash.

This is useful in a situation where you may want to mirror an upstream dynamically, caching the results forever but only when the content
matches a known checksum.

Setting ``reject_unmatched`` to ``true`` will prevent requests with paths that are not matched by the filter from passing through.

Example configuration
---------------------

Full filter configuration:

.. literalinclude:: _include/checksum_filter.yaml
    :language: yaml
    :lines: 26-38
    :emphasize-lines: 2-12
    :linenos:
    :caption: :download:`checksum_filter.yaml <_include/checksum_filter.yaml>`
