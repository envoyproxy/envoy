.. _config_network_filters_sni_to_metadata:

SNI-to-Metadata Filter
=======================

.. attention::

  SNI-to-Metadata Filter support should be considered alpha and not production ready.


The SNI-to-Metadata Filter is a filter that extracts the SNI of the client connection and stores it in the connection dynamic metadata.
It is able to conditionally extract based on regex patters as well as extract fields and format the metadata using regex capture groups.

Example Configuration
----------------------

.. code-block:: yaml

  network_filters:
  - name: envoy.filters.network.sni_to_metadata
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.filters.network.sni_to_metadata.v3.SniToMetadataFilter
      connection_rules:
        - pattern: ^([^.]+)\.([^.]+)\.([^.]+)\.example\.com$
          metadata_targets:
            - metadata_key: app_name
              metadata_namespace: envoy.lb
              metadata_value: \1
