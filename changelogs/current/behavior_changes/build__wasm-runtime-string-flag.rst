The ``--define wasm=<engine>`` and ``--define engine=<engine>`` build flags for selecting the
WebAssembly runtime have been replaced by the first-class Bazel build setting
``--@proxy-wasm-cpp-host//bazel:engine=<engine>``. Accepted values are ``v8`` (default),
``wamr`` (interpreter mode), ``wamr-interp``, ``wamr-jit``, ``wasmtime``, ``null``,
``disabled``, and ``multi``. The old ``--define`` flags are no longer honoured and will raise
a failure. Users and CI scripts must update their invocations.
