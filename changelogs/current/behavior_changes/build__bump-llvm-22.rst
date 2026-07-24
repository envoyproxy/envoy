Bumped the hermetic LLVM/Clang toolchain from 18 to 22. This upgrades the
default compiler used by ``--config=clang`` and may surface new warnings or
diagnostics in downstream builds that pin to the Envoy toolchain.
