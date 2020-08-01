.. _arch_overview_compression:

Compression
===========

Underlying implementation
-------------------------

Currently Envoy is written to use `zlib <http://zlib.net>`_ as the compression provider.

.. note::

  `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ is a fork that hosts several 3rd-party
  contributions containing new optimizations. Those optimizations are considered useful for
  `improving compression performance <https://github.com/envoyproxy/envoy/issues/8448#issuecomment-667152013>`_.
  Envoy can be built with `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ support by using
  ``--define zlib=ng`` Bazel option. Currently, this option is only available on Linux-x86_64.
