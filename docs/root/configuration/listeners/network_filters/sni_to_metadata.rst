.. _config_network_filters_sni_to_metadata:

SNI to Metadata Filter
=======================

.. attention::

  SNI to Metadata Filter support should be considered alpha and not production ready.

The SNI to Metadata Filter is a filter that extracts metadata from the SNI of the client connection and stores it in the connection dynamic metadata.

It is able to conditionally extract based on regex patters as well as extract fields and format the metadata using regex capture groups.