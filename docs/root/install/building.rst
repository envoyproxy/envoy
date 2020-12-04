.. _building:


构建
====

Envoy 使用 Bazel 来构建系统。为了更简单地初始化构建和快速开始，我们提供了一个 Ubuntu 16 的基础镜像容器，这个容器包含了需要构建和 *静态链接* Envoy 的所有工具，详情可查看 :repo:`ci/README.md`。

为了能够手动构建，可参照 :repo:`bazel/README.md` 中的使用说明。

.. _install_requirements:

必备条件
--------

Envoy 最初实在 Ubuntu 14.04 LTS 上开发和部署的。它可以在任何最近的 Linux 上运行，包括 Ubuntu 18.04 LTS。

构建 Envoy 需要具备以下条件：
* GCC 7+ 或者 Clang/LLVM 7+ (支持 C++14)。 使用 Clang 时，Clang/LLVM 9+ 时首选 (如下)。
* 这些 :repo:`Bazel 原生 <bazel/repository_locations.bzl>` 依赖。


关于执行手工构建的更多信息，请看链接 :repo:`CI <ci/README.md>` 和 :repo:`Bazel <bazel/README.md>` 中的文档。
需要注意的是 Clang/LLVM 8 或者更低版本，Envoy 可能需要用 `--define tcmalloc=gperftools` 参数来构建，因为新的 tcmalloc 代码不能够保证用较低版本的 Clang 来编译。

.. _install_binaries:

预制二进制
----------

当我们发布官方版本时，我们会用版本号来构建镜像并给它打标签。可以在下面的仓库中找到这些镜像：

* `envoyproxy/envoy <https://hub.docker.com/r/envoyproxy/envoy/tags/>`_：在 Ubuntu Bionic 基础之上发布消除符号的二进制包。
* `envoyproxy/envoy-debug <https://hub.docker.com/r/envoyproxy/envoy-debug/tags/>`_: 在 Ubuntu Bionic 基础之上发布带有调试符号的二进制包。
* `envoyproxy/envoy-alpine <https://hub.docker.com/r/envoyproxy/envoy-alpine/tags/>`_: 在 **glibc** alpine 基础之上发布消除符号的二进制包。
* `envoyproxy/envoy-alpine-debug <https://hub.docker.com/r/envoyproxy/envoy-alpine-debug/tags/>`_: *为了支持 envoyproxy/envoy-debug，此仓库已被弃用*。 在 **glibc** alpine 基础之上发布带有调试符号的二进制包。

.. note::

  在上面的仓库中，我们给每一个安全/稳定的版本打一个标签为 *vX.Y-latest* 的镜像。


针对每一个主分支提交，我们额外地创建了一套开发 Docker 镜像。在下面的仓库中可以找到这些镜像：

* `envoyproxy/envoy-dev <https://hub.docker.com/r/envoyproxy/envoy-dev/tags/>`_: 在 Ubuntu Bionic 基础之上发布消除符号的二进制>包。
* `envoyproxy/envoy-debug-dev <https://hub.docker.com/r/envoyproxy/envoy-debug-dev/tags/>`_: 在 Ubuntu Bionic 基础之上发布带有>调试符号的二进制包。
* `envoyproxy/envoy-alpine-dev <https://hub.docker.com/r/envoyproxy/envoy-alpine-dev/tags/>`_: 在 **glibc** alpine 基础之上发布消除符号的二进制包。
* `envoyproxy/envoy-alpine-debug-dev <https://hub.docker.com/r/envoyproxy/envoy-alpine-debug-dev/tags/>`_: *为了支持 envoyproxy/envoy-debug-dev，此仓库已被弃用*。在 **glibc** alpine 基础之上发布带有调试符号的二进制包。

在上述的 *dev* 仓库中，*latest* 标签指向主分支中通过测试的最近的 Envoy SHA。

.. note::

  Envoy 项目始终视主分支（master）为版本发布的候选分支，很多组织使用主分支在生产中进行追踪和部署。我们鼓励大家做同样的   事情，以便在开发流程中能够尽早的报告遇到的问题。

可以在 `GetEnvoy.io <https://www.getenvoy.io/>`_ 找到为不同平台打包的 Envoy 预制二进制包。

我们将会根据社区对 CI、打包方面的兴趣来生成额外的二进制包类型。针对不同平台的预制二进制包，请开一个 `GetEnvoy issue <https://github.com/tetratelabs/getenvoy/issues>`_ 。

.. _arm_binaries:

ARM64 二进制
^^^^^^^^^^^^^^

`envoyproxy/envoy <https://hub.docker.com/r/envoyproxy/envoy/tags/>`_,
`envoyproxy/envoy-debug <https://hub.docker.com/r/envoyproxy/envoy-debug/tags/>`_,
`envoyproxy/envoy-dev <https://hub.docker.com/r/envoyproxy/envoy-dev/tags/>`_ 和
`envoyproxy/envoy-debug-dev <https://hub.docker.com/r/envoyproxy/envoy-debug-dev/tags/>`_ 是 docker
`multi-arch <https://www.docker.com/blog/multi-arch-build-and-images-the-simple-way/>`_ 镜像且在兼容的 ARM64 主机上面应该是透明运行的。


修改 Envoy
-----------

如果你对于修改 Envoy 和测试你的改变感兴趣，一种方式就是使用 Docker。这个指导将讲述如何构建你自己的 Envoy 二进制包，并且把包放在一个 Ubuntu 容器中。

.. toctree::
    :maxdepth: 2

    sandboxes/local_docker_build
