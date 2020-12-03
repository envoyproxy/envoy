.. _install_sandboxes_local_docker_build:

构建一个 Envoy Docker 镜像
==========================

以下步骤引导你构建自己的 Envoy 二进制文件，并且将它放在一个干净的 Ubuntu 容器中。

**步骤 1：构建 Envoy**

使用 ``envoyproxy/envoy-build`` 镜像来编译 Envoy。
这个镜像包含构建 Envoy 所需的软件。从你的 Envoy 目录开始::

  $ pwd
  src/envoy
  $ ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.release'

上述命令需要一点时间来执行，因为它需要编译一个 Envoy 二进制文件并运行测试。

关于构建和不同构建目标的更多信息，请参考 :repo:`ci/README.md` 。

**步骤 2：构建只有 Envoy 二进制文件的镜像**

下面的步骤，我们将构建一个只有 Envoy 二进制文件的镜像，且并不使用任何软件来构建此镜像。::

  $ pwd
  src/envoy/
  $ docker build -f ci/Dockerfile-envoy -t envoy .

你可以在任意 Dockerfile 的 FROM 行中引用这个镜像，这样就可以基于这个 ``envoy`` 镜像构建出任意的沙盒环境。

如果你对修改 Envoy 且测试你的修改感兴趣，这将是非常有用的。
