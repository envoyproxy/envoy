以下文档说明了上述对于 Envoy 容器编译环境进行设置的过程。

步骤 1：安装 Docker
********************

确保你已安装较新版本的 ``docker`` 和 ``docker-compose`` 。

一个最简单的方法来完成此事就是使用 `Docker Desktop <https://www.docker.com/products/docker-desktop>`_ 。

步骤 2：克隆 Envoy 仓库
*************************


如果你还没有克隆 Envoy 仓库，用如下方式进行克隆：

.. tabs::

   .. code-tab:: console SSH

      git clone git@github.com:envoyproxy/envoy

   .. code-tab:: console HTTPS

      git clone https://github.com/envoyproxy/envoy.git
