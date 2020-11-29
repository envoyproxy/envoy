.. _install_sandboxes_local_docker_build:

构建一个 Envoy Docker 镜像
==========================

以下步骤引导你构建自己的 Envoy 二进制包，并且将它放在一个干净的 Ubuntu 容器中。

**步骤 1：构建 Envoy**

使用 ``envoyproxy/envoy-build`` 你将开始编译 Envoy。
这个镜像包含所有构建 Envoy 所需的软件。从你的 Envoy 目录开始::

  $ pwd
  src/envoy
  $ ./ci/run_envoy_docker.sh './ci/do_ci.sh bazel.release'

上述命令需要一点时间来执行因为它正在编译一个 Envoy 二进制包且要运行测试。

关于构建和不同构建目标的更多信息，请参考 :repo:`ci/README.md` 。

**步骤 2：构建只有 Envoy 二进制包的镜像**

下面的步骤，我们将构建一个只有 Envoy 二进制包的镜像，且并不使用任何软件来构建此镜像。::

  $ pwd
  src/envoy/
  $ docker build -f ci/Dockerfile-envoy -t envoy .

如果你在任何 Dockerfile 中改变了 ``FROM`` 行，那么现在你可以使用这个 ``envoy`` 镜像来构建任何沙盒环境。

如果你对修改 Envoy 且测试你的修改感兴趣，这将是非常有用的。
