.. _dev_performance_size:

Analysis of binary size
=======================

In order to be able to tackle binary size analysis,
the Envoy Mobile team has standardized the process in this document.

Object files analysis
---------------------

Getting a binary
~~~~~~~~~~~~~~~~

In order to have consistency of results this is the toolchain used to build the
binary for analysis:

1. clang-8
2. lld (installed with clang)
3. arm64 machine

The arm64 machine used to build the test binary was an arm64 ec2 a1.2xlarge
instance with Ubuntu 18.04. The following script was run to set up the
necessary tools::

  # basic toolchain
  sudo apt-get install build-essential openjdk-8-jdk python zip unzip \
    software-properties-common make cmake bc libtool ninja-build automake \
    time apt-transport-https
  # clang-8
  wget http://releases.llvm.org/8.0.0/clang+llvm-8.0.0-aarch64-linux-gnu.tar.xz
  sudo tar -C /usr/local/ -xvf clang+llvm-8.0.0-aarch64-linux-gnu.tar.xz --strip 1
  rm -rf clang+llvm-8.0.0-aarch64-linux-gnu.tar.xz
  # bazel 0.26.1
  wget https://github.com/bazelbuild/bazel/releases/download/0.26.1/bazel-0.26.1-dist.zip
  mkdir -p /tmp/bazel_build
  unzip -o bazel-0.26.1-dist.zip -d /tmp/bazel_build
  rm -rf bazel-0.26.1-dist.zip
  cd /tmp/bazel_build
  env EXTRA_BAZEL_ARGS="--host_javabase=@local_jdk//:jdk" bash ./compile.sh
  sudo cp output/bazel /usr/local/bin/bazel
  rm -rf /tmp/bazel_build
  # bloaty
  git clone https://github.com/google/bloaty
  cd bloaty
  mkdir build
  cd build
  cmake ..
  make -j6
  cp bloaty /usr/local/bin/bloaty

The binary being compiled is ``//test/performance:test_binary_size``.
The binary is getting built with the following build command::

  ./bazelw build //test/performance:test_binary_size --config=sizeopt --copt=-ggdb3 --linkopt=-fuse-ld=lld

Thus the binary is compiled with the following flags pertinent to reducing
binary size:

.. _envoy_docs: https://github.com/envoyproxy/envoy/blob/master/bazel/README.md#enabling-optional-features

1. ``-c opt``: bazel compilation option for size optimization. Due to ``sizeopt``.
2. ``--copt -Os``: optimize for size. Due to ``sizeopt``.
3. ``--copt=-ggdb3``: keep debug symbols. Later stripped with ``strip``. Explicitly added.
4. ``--linkopt=-fuse-ld=lld``: use the lld linker. Explicitly added.
5. ``--define=google_grpc=disabled``: more info in the `envoy docs <envoy_docs>`_. Due to the project's ``.bazelrc``.
6. ``--define=signal_trace=disabled``: more info in the `envoy docs <envoy_docs>`_. Due to the project's ``.bazelrc``.
7. ``--define=tcmalloc=disabled``: more info in the `envoy docs <envoy_docs>`_. Due to the project's ``.bazelrc``.
8. ``--define=hot_restart=disabled``: more info in the `envoy docs <envoy_docs>`_. Due to the project's ``.bazelrc``.
9. ``--define=envoy_mobile_xds=disabled``: more info in the `envoy docs <envoy_docs>`_. Due to the project's ``.bazelrc``.

After compiling, the binary can be stripped of all symbols by using ``strip``::

  strip -s bazel-bin/test/performance/test_binary_size

The unstripped and stripped binary can then be used for analysis.

Analysis
~~~~~~~~

While there are a lot of tools out there that can be used for binary size
analysis (otool, objdump, jtool), `Bloaty <https://github.com/google/bloaty>`_
has been the tool of choice to run object file analysis.

Bloaty's layering of data sources is extremely helpful in being able to explode
the binary in all sorts of different ways. For example, one can look at the
composition of each compile unit in terms of sections::

  $ bloaty --debug-file=bin/test_binary_size -c envoy.bloaty -d sections,bloaty_package,compileunits bin/test_binary_size.stripped
  ...
  7.7%   109Ki   7.7%   110Ki bazel-out/aarch64-opt/bin/external/envoy_api/envoy/api/v2/route/route.pb.cc
    81.9%  89.6Ki  81.4%  89.6Ki .text
    13.6%  14.9Ki  13.5%  14.9Ki .eh_frame
    3.1%  3.45Ki   3.1%  3.45Ki .eh_frame_hdr
    1.3%  1.48Ki   1.3%  1.48Ki .data
    0.0%       0   0.6%     672 .bss
    0.1%      72   0.1%      72 .rodata

Or one might want to see how sections of the binary map to compilation units::

  $ bloaty --debug-file=bin/test_binary_size -c envoy.bloaty -d bloaty_package,compileunits,sections bin/test_binary_size.stripped
  ...
  13.2%   929Ki  13.0%   929Ki .rodata
      81.2%   755Ki  81.2%   755Ki [section .rodata]
      15.4%   143Ki  15.4%   143Ki boringssl/
          37.9%  54.4Ki  37.9%  54.4Ki external/boringssl/src/crypto/obj/obj.c
          21.7%  31.1Ki  21.7%  31.1Ki external/boringssl/src/third_party/fiat/curve25519.c
          17.3%  24.9Ki  17.3%  24.9Ki external/boringssl/src/crypto/fipsmodule/bcm.c
          11.3%  16.2Ki  11.3%  16.2Ki external/boringssl/err_data.c
           4.5%  6.39Ki   4.5%  6.39Ki [55 Others]
           0.9%  1.26Ki   0.9%  1.26Ki external/boringssl/src/crypto/x509v3/v3_crld.c
           0.7%  1.06Ki   0.7%  1.06Ki external/boringssl/src/crypto/asn1/tasn_typ.c

These different representations will give you perspective about how different
changes in the binary will affect size. Note that the ``envoy.bloaty`` config
refers to a bloaty config that has regexes to capture output. The example
config used in this type of analysis is::

  custom_data_source: {
    name: "bloaty_package"
    base_data_source: "compileunits"

    #envoy source code.
    rewrite: {
      pattern: "^(external/envoy/source/)(\\w+/)(\\w+)"
      replacement: "envoy \\2"
    }

    #envoy third party libraries.
    rewrite: {
        pattern: "^(external/)(\\w+/)"
        replacement: "\\2"
    }

    #all compiled protos.
    rewrite: {
        pattern: "([.pb.cc | .pb.validate.cc])$"
        replacement: "compiled protos"
    }
  }

Open issues regarding size
--------------------------

``perf/size`` is a label tagging all current open issues that can improve
binary size. Check out the issues `here
<https://github.com/envoyproxy/envoy-mobile/labels/perf%2Fsize>`_. After performing
any change that tries to address these issues you should run through the
analysis pipeline described above, and make sure your changes match
expectations.

Current status
--------------

iOS
~~~

When compiling Envoy Mobile for ``arm64`` only, we found the final size to be
**approximately 4.6 MB** as of :tree:`v0.2.3.03062020 <v0.2.3.03062020>`.

This analysis was done by:

- Compiling the `analysis variant example app <https://github.com/rebello95/EnvoyMobileAnalysis/tree/v0.2.3.03062020/AnalysisVariant>`_ for release
- Exporing the app for Ad Hoc distribution using Xcode
- Enabling app thinning for ``arm64`` only
- Investigating the ``.ipa`` file and/or ``App Thinning Size Report.txt``
- Doing the same for the `analysis control example app <https://github.com/rebello95/EnvoyMobileAnalysis/tree/v0.2.3.03062020/AnalysisControl>`_ in that repository, and comparing the size differences

::

  Control:
  App + On Demand Resources size: 27 KB compressed, 113 KB uncompressed
  App size: 27 KB compressed, 113 KB uncompressed

  Variant:
  App + On Demand Resources size: 4.6 MB compressed, 13.8 MB uncompressed
  App size: 4.6 MB compressed, 13.8 MB uncompressed

  Net: 4.6 MB compressed, 13.8 MB uncompressed

Android
~~~~~~~

This is being done in :issue:`#742 <742>`.

CI integration
--------------

CI validates that no PR increases the binary size of the library above a specific
threshold specified in the :repo:`perf.yml configuration <.github/workflows/perf.yml>`.

The status of this job is reported on PRs in the ``perf / size_compare`` task.
