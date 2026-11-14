The GCC reference build (``--config=gcc``) is now built and tested in Envoy CI with ``-c opt``, for
consistency with the Clang build, which is built and tested with ``-c opt``. The new ``gcc-ci``
config builds it as CI does, without debug information (``--fission=no`` and ``-g0``): with GCC, the
split debug information that ``-c opt`` adds took compiles and links over the memory of the RBE
pools they run on and made the binaries several times larger, and nothing from the GCC build is
published.

``--config=gcc`` now links with ``-z nostart-stop-gc``, as ``-c opt`` links with ``--gc-sections``,
which otherwise drops the link array sections ``upb`` registers extensions in. It also no longer
treats ``-Wnonnull`` and ``-Wstringop-overflow`` as errors, as GCC reports false positives for them
with optimization.

This has no effect on any production binaries produced by the project, but downstream projects that
build with GCC and wish to continue building with ``-c fastbuild`` may require some adjustment.
