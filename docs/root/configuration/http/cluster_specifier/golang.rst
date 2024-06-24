.. _config_http_cluster_specifier_golang:

Golang cluster specifier
========================

The HTTP Golang cluster specifier allows `Golang <https://go.dev/>`_ to choose router cluster
and makes it easier to extend Envoy.

Go cluster specifier plugins can be recompiled independently of Envoy.

.. warning::
  The Envoy Golang cluster specifier is designed to be run with the ``GODEBUG=cgocheck=0`` environment variable set.

  This disables the cgo pointer check.

  Failure to set this environment variable will cause Envoy to crash!
