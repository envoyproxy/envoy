.. _start:

开始
====

本节让你从一个非常简单的配置开始，并提供了一些配置示例。

开始使用 Envoy 的最快速方式就是 :ref:`安装预制的二进制文件 <install_binaries>`。你也可以选择从源码进行 :ref:`构建 <building>`。

这些示例使用 :ref:`v3 Envoy API <envoy_api_reference>`，但是仅仅使用了 API 的静态配置特性，这些对于简单需求来讲是最有用的。:ref:`动态配置 <arch_overview_dynamic_config>` 支持更多的复杂需求。

快速开始运行简单示例
---------------------

这些指导说明使用 Envoy 仓库中的文件来运行示例。下面的章节对配置文件和相同配置下的执行步骤做了更详细的解释。

在 :repo:`configs/google_com_proxy.v2.yaml`  中提供了一个非常简单的 Envoy 配置，可以用来验证基本的纯 HTTP 代理。但这并不代表真实的 Envoy 部署：

.. substitution-code-block:: none

  $ docker pull envoyproxy/|envoy_docker_image|
  $ docker run --rm -d -p 10000:10000 envoyproxy/|envoy_docker_image|
  $ curl -v localhost:10000

使用的 Docker 镜像将包含最新版本的 Envoy 和一个基本的 Envoy 配置。这个基本的配置告诉 Envoy 将传入请求路由到 \*.google.com。

简单配置
--------

可以在命令行中以参数的形式传入一个简单的 YAML 文件来配置 Envoy。

需要 :ref:`管理信息 <envoy_v3_api_msg_config.bootstrap.v3.Admin>` 来配置管理服务器。`address` 键指定了监听 :ref:`地址 <envoy_v3_api_file_envoy/config/core/v3/address.proto>`，在这个例子中是 `0.0.0.0:9901`。

.. code-block:: yaml

  admin:
    access_log_path: /tmp/admin_access.log
    address:
      socket_address: { address: 0.0.0.0, port_value: 9901 }

:ref:`static_resources <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.static_resources>` 包含了当 Envoy 启动时静态配置的一切资源，与之相反的是当 Envoy 运行时动态配置的资源。:ref:`v2 API 概览 <config_overview>` 描述了这些。

.. code-block:: yaml

    static_resources:

:ref:`监听器 <envoy_v3_api_file_envoy/config/listener/v3/listener.proto>` 说明。

.. code-block:: yaml

      listeners:
      - name: listener_0
        address:
          socket_address: { address: 0.0.0.0, port_value: 10000 }
        filter_chains:
        - filters:
          - name: envoy.filters.network.http_connection_manager
            typed_config:
              "@type": type.googleapis.com/envoy.extensions.filters.network.http_connection_manager.v3.HttpConnectionManager
              stat_prefix: ingress_http
              codec_type: AUTO
              route_config:
                name: local_route
                virtual_hosts:
                - name: local_service
                  domains: ["*"]
                  routes:
                  - match: { prefix: "/" }
                    route: { host_rewrite_literal: www.google.com, cluster: service_google }
              http_filters:
              - name: envoy.filters.http.router

:ref:`集群 <envoy_v3_api_file_envoy/service/cluster/v3/cds.proto>` 说明。

.. code-block:: yaml

      clusters:
      - name: service_google
        connect_timeout: 0.25s
        type: LOGICAL_DNS
        # Comment out the following line to test on v6 networks
        dns_lookup_family: V4_ONLY
        lb_policy: ROUND_ROBIN
        load_assignment:
          cluster_name: service_google
          endpoints:
          - lb_endpoints:
            - endpoint:
                address:
                  socket_address:
                    address: www.google.com
                    port_value: 443
        transport_socket:
          name: envoy.transport_sockets.tls
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.UpstreamTlsContext
            sni: www.google.com


使用 Envoy Docker 镜像
----------------------

创建一个简单的 Dockerfile 运行 Envoy，这需要假定在你本地的目录中有 envoy.yaml （如上描述）文件。

.. substitution-code-block:: none

  FROM envoyproxy/|envoy_docker_image|
  COPY envoy.yaml /etc/envoy/envoy.yaml

使用你的配置并用如下命令构建 Docker 镜像::

  $ docker build -t envoy:v1 .

现在，你可以用如下命令启动 Envoy 容器了::

  $ docker run -d --name envoy -p 9901:9901 -p 10000:10000 envoy:v1

最后，你可以用如下命令来进行测试::

  $ curl -v localhost:10000

如果你想用 docker-compose 来使用 Envoy，你可以用一个 volume 来覆写提供的配置文件。

.. substitution-code-block: yaml

  version: '3'
  services:
    envoy:
      image: envoyproxy/|envoy_docker_image|
      ports:
        - "10000:10000"
      volumes:
        - ./envoy.yaml:/etc/envoy/envoy.yaml

默认情况下，Docker 镜像将以构建时创建的 ``envoy`` 用户来运行。

``envoy`` 用户的 ``uid`` 和 ``gid`` 可以在运行时使用 ``ENVOY_UID`` 和 ``ENVOY_GID`` 这两个环境变量来设定。这也可以在 Docker 命令行中来完成设定，比如::

  $ docker run -d --name envoy -e ENVOY_UID=777 -e ENVOY_GID=777 -p 9901:9901 -p 10000:10000 envoy:v1

如果你想对容器内部的 ``unix`` 套接字进行限制或者提供访问，抑或从容器外部控制 ``envoy`` 套接字的访问，这种方式是非常有用的。

如果你想以 ``root`` 用户来运行容器，你可以将 ``ENVOY_UID`` 设置为 ``0``。

默认情况下，``envoy`` 镜像会把应用程序的日志发送到 ``/dev/stdout`` 和 ``/dev/stderr`` ，这样就可以在容器日志中看到了。

如果你把应用程序日志、管理和访问日志输出到一个文件，``envoy`` 用户将需要足够的权限来写这个文件。这个可以通过设置 ``ENVOY_UID`` 和/或者通过将文件变成 envoy 用户可写的方法来实现。

例如，在主机上挂载一个日志文件夹并且让它是可写的，你可以采取如下操作：

.. substitution-code-block:: none

  $ mkdir logs
  $ chown 777 logs
  $ docker run -d -v `pwd`/logs:/var/log --name envoy -e ENVOY_UID=777 -p 9901:9901 -p 10000:10000 envoy:v1

You can then configure ``envoy`` to log to files in ``/var/log``

随后，你可以配置 ``envoy`` 将日志文件输出在 ``/var/log`` 文件里。

``envoy`` ``uid`` 和 ``gid`` 的默认值都是 ``101`` 。

``envoy`` 用户还需要权限能够去访问被挂载到容器内部的任何需要的配置文件。

如果你运行的环境中有一个严格的 ``umask`` 设置，你可能需要通过设置文件的 ``uid`` 或 ``gid`` 来给 envoy 提供访问权限，或者将配置文件设置为对 envoy 用户可读的。

有一种可以不用改变任何文件的权限或者在容器内部使用 root 用户的方法就是在启动容器的时候使用宿主机用户的 ``uid`` ，例如：

.. substitution-code-block:: none

  $ docker run -d --name envoy -e ENVOY_UID=`id -u` -p 9901:9901 -p 10000:10000 envoy:v1


沙盒
----

我们已经创建了一些使用 Docker 组件的沙盒环境，来设定不同的环境以进行 Envoy 特性的测试和示例配置的展示。随着我们对人们兴趣的判定，我们将添加更多的沙盒来验证不同的特性，如下是一些可用的沙盒：

.. toctree::
    :maxdepth: 2

    sandboxes/cache
    sandboxes/cors
    sandboxes/csrf
    sandboxes/ext_authz
    sandboxes/fault_injection
    sandboxes/front_proxy
    sandboxes/grpc_bridge
    sandboxes/jaeger_native_tracing
    sandboxes/jaeger_tracing
    sandboxes/load_reporting_service
    sandboxes/lua
    sandboxes/mysql
    sandboxes/redis
    sandboxes/zipkin_tracing
