.. _config_http_filters_file_server:

File Server
===========

The file server filter can be used to respond with the contents of a file from the filesystem.

The ``content-length`` header will be the size of the file.

The ``content-type`` header will be set based on filename suffix and filter configuration.

.. note::

 This filter is not yet supported on Windows.

Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.file_server.v3.FileServerConfig``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.file_server.v3.FileServerConfig>`
