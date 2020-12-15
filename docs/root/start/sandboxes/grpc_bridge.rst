.. _install_sandboxes_grpc_bridge:

gRPC 网桥
===========

Envoy gRPC
~~~~~~~~~~

gRPC 网桥沙盒是 Envoy
:ref:`gRPC 网桥过滤器 <config_http_filters_grpc_bridge>` 的一个示例用法。

该示例是用 ``Python`` 编写的基于 ``http`` 客户端的 CLI，
更新由 ``Go`` 编写的远程存储的键值存储示例，并且这两种语言都使用 stub 生成代码。

客户端通过代理发送消息，将 HTTP 请求从 ``http/1.1`` 升级到 ``http/2``。

``[客户端](http/1.1) -> [客户端出口代理](http/2) -> [服务端入口代理](http/2) -> [服务端]``

本示例中演示 Envoy 的另一个功能是 Envoy 能够通过路由的配置实现基于路由的授权。


运行沙盒
~~~~~~~~~~~~~~~~~~~

.. include:: _include/docker-env-setup.rst

步骤 3：生成协议 stub
***********************************

``protos`` 目录中提供了一个 docker-compose 文件，用于为 ``客户端`` 和 ``服务端`` 生成 stub。

检查 ``docker-compose-protos.yaml`` 文件， 你将看到包含生成协议 stub 所需的 ``python``
和 ``go`` 的 gRPC protoc 命令。

生成 stub 的过程如下所示：

.. code-block:: console

  $ pwd
  envoy/examples/grpc-bridge
  $ docker-compose -f docker-compose-protos.yaml up
  Starting grpc-bridge_stubs_python_1 ... done
  Starting grpc-bridge_stubs_go_1     ... done
  Attaching to grpc-bridge_stubs_go_1, grpc-bridge_stubs_python_1
  grpc-bridge_stubs_go_1 exited with code 0
  grpc-bridge_stubs_python_1 exited with code 0

你可以使用以下命令清理剩余的容器：

.. code-block:: console

  $ docker container prune

你可以在客户端和服务端各自的目录中查看生成的 ``kv`` 模块:

.. code-block:: console

  $ ls -la client/kv/kv_pb2.py
  -rw-r--r--  1 mdesales  CORP\Domain Users  9527 Nov  6 21:59 client/kv/kv_pb2.py

  $ ls -la server/kv/kv.pb.go
  -rw-r--r--  1 mdesales  CORP\Domain Users  9994 Nov  6 21:59 server/kv/kv.pb.go

这些生成的 ``python`` 和 ``go`` 的 stub 可以作为外部模块包含。

步骤 4：启动所有容器
***********************************

构建沙盒示例并启动示例服务，运行以下命令：

.. code-block:: console

    $ pwd
    envoy/examples/grpc-bridge
    $ docker-compose pull
    $ docker-compose up --build -d
    $ docker-compose ps

                   Name                             Command               State                         Ports
    ---------------------------------------------------------------------------------------------------------------------------------------
    grpc-bridge_grpc-client-proxy_1        /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:9911->9911/tcp, 0.0.0.0:9991->9991/tcp
    grpc-bridge_grpc-client_1              /bin/sh -c tail -f /dev/null   Up
    grpc-bridge_grpc-server-proxy_1        /docker-entrypoint.sh /bin ... Up      10000/tcp, 0.0.0.0:8811->8811/tcp, 0.0.0.0:8881->8881/tcp
    grpc-bridge_grpc-server_1              /bin/sh -c /bin/server         Up      0.0.0.0:8081->8081/tcp


发送请求到键/值存储
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

使用 Python 服务发送 gRPC 请求：

.. code-block:: console

  $ pwd
  envoy/examples/grpc-bridge

设置键:

.. code-block:: console

  $ docker-compose exec python /client/client.py set foo bar
  setf foo to bar


获取键:

.. code-block:: console

  $ docker-compose exec python /client/client.py get foo
  bar

修改存在的键:

.. code-block:: console

  $ docker-compose exec python /client/client.py set foo baz
  setf foo to baz

获取修改后的键:

.. code-block:: console

  $ docker-compose exec python /client/client.py get foo
  baz

在运行的 docker-compose 容器中，你应该看到 gRPC 服务打印的活动记录：

.. code-block:: console

  $ docker-compose logs grpc-server
  grpc_1    | 2017/05/30 12:05:09 set: foo = bar
  grpc_1    | 2017/05/30 12:05:12 get: foo
  grpc_1    | 2017/05/30 12:05:18 set: foo = baz
