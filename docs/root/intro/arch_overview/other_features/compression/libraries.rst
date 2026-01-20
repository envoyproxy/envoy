.. _arch_overview_compression_libraries:

Compression Libraries
=====================

Underlying implementation
-------------------------

Currently Envoy uses `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_, `brotli <https://brotli.org>`_ and
`zstd <https://facebook.github.io/zstd>`_ as compression libraries.

.. note::

  `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ is a fork that hosts several 3rd-party
  contributions containing new optimizations. Those optimizations are considered useful for
  `improving compression performance <https://github.com/envoyproxy/envoy/issues/8448#issuecomment-667152013>`_.
  Envoy is now by default built using `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ instead of regular
  `zlib <http://zlib.net>`_, but can be linked to `zlib <http://zlib.net>`_ by using ``--define zlib=original``
  Bazel option. The relevant build options used to build `zlib-ng <https://github.com/zlib-ng/zlib-ng>`_ can be
  evaluated in :repo:`here <bazel/foreign_cc/BUILD>`.
