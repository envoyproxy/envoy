The ``--define wasm=<engine>`` build flag for selecting the WebAssembly runtime has been replaced
by the first-class Bazel build setting ``--//bazel:wasm_runtime=<engine>``
(or ``--@envoy//bazel:wasm_runtime=<engine>`` in external build repositories). Accepted values are
``v8`` (default), ``wamr``, ``wasmtime``, and ``disabled``. The old ``--define wasm=...`` flag is
no longer honoured and will be **silently ignored** rather than producing an error, causing builds
to fall back to the ``v8`` default. Users and CI scripts must update their invocations.
Note: ``bazelrc.windows`` still contains ``--define wasm=disabled`` and has **not** been updated in
this change; a follow-up is required to switch that line to
``build:windows --//bazel:wasm_runtime=disabled``.
