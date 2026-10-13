Switched the Bazel build from ``WORKSPACE`` to bzlmod (``MODULE.bazel``). The ``WORKSPACE`` file has
been removed and ``--enable_bzlmod`` / ``--noenable_workspace`` are now set in ``.bazelrc``; the
``--enable_workspace`` and ``--noenable_bzlmod`` flags added in 1.39 are no longer required or
supported. Envoy, the Envoy API and the Envoy docs are now published as Bazel modules (``envoy``,
``envoy_api`` and ``envoy-docs``).

Downstream projects that build against Envoy must migrate to bzlmod. In addition to the Bazel Central
Registry, downstreams **must** add and track Envoy's custom registry
(``https://raw.githubusercontent.com/envoyproxy/bazel-registry``) via ``--registry``, as many of
Envoy's dependencies and patched modules are only published there. Dependency wiring such as the
crypto/SSL/zlib implementations used by ``quiche``, ``proxy-wasm-cpp-host`` and ``libsxg`` is now
configured via ``label_flag`` settings in ``.bazelrc``, and canonical bzlmod repository names apply
under ``bazel-bin/external`` (e.g. ``su-exec+`` rather than ``su-exec``), which may affect scripts
referencing those paths. A complete, working downstream example — including ``MODULE.bazel``,
``.bazelrc``, registry configuration, toolchain setup and a custom extension build config — is
available in ``bazel/tests/external``.
