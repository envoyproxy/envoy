.. _config_http_filters_golang:

Golang
======

Overview
--------

The HTTP Golang filter allows `Golang <https://go.dev/>`_ to be run during both the request
and response flows and makes it easier to extend Envoy. See the `Envoy's GoLang extension proposal documentation
<https://docs.google.com/document/d/1noApyS0IfmOGmEOHdWk2-BOp0V37zgXMM4MdByr1lQk/edit?usp=sharing>`_ for more details.


Configuration
-------------

* This filter should be configured with the type URL ``type.googleapis.com/envoy.extensions.filters.http.golang.v3alpha.Config``.
* :ref:`v3 API reference <envoy_v3_api_msg_extensions.filters.http.golang.v3alpha.Config>`

A simple example of configuring Golang HTTP filter that default `echo` go plugin as follow:

.. code-block:: yaml

    name: envoy.filters.http.golang
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.filters.http.golang.v3alpha.Config
          library_id: echo-id
          library_path: "contrib/golang/filters/http/test/test_data/echo/filter.so"
          plugin_name: echo

.. attention::

  The go plugin dynamic library built needs to be consistent with the envoy version of glibc.

Complete example
----------------

A complete example using Docker is available in :repo:`/contrib/golang/filters/http/test/test_data/echo` and run
``bazel build //contrib/golang/filters/http/test/test_data/echo:filter.so``.
