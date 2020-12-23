.. _config_runtime:

运行时
=======

:ref:`运行时配置 <arch_overview_runtime>` 详述了一个包含有可重复加载配置元素的虚拟文件系统。可以通过
一系列本地文件系统、静态 bootstrap 配置、RTDS 和管理控制台衍生的 overlays 来实现虚拟文件系统。

* :ref:`v3 API 参考 <envoy_v3_api_msg_config.bootstrap.v3.Runtime>`

.. _config_virtual_filesystem:

虚拟文件系统
-------------

.. _config_runtime_layering:

分层
+++++

运行时可被视为是一个包含多层的虚拟文件系统。:ref:`层级运行时 <envoy_v3_api_msg_config.bootstrap.v3.LayeredRuntime>` 
bootstrap 配置详述了这个分层。后一层中的运行时设置会覆盖前一层。一种典型的配置会是：

.. validated-code-block:: yaml
  :type-name: envoy.config.bootstrap.v3.LayeredRuntime

  layers:
  - name: static_layer_0
    static_layer:
      health_check:
        min_interval: 5
  - name: disk_layer_0
    disk_layer: { symlink_root: /srv/runtime/current, subdirectory: envoy }
  - name: disk_layer_1
    disk_layer: { symlink_root: /srv/runtime/current, subdirectory: envoy_override, append_service_cluster: true }
  - name: admin_layer_0
    admin_layer: {}

在弃用的 :ref:`运行时 <envoy_v3_api_msg_config.bootstrap.v3.Runtime>` bootstrap 配置中，分层是隐式且固定的：

1. :ref:`静态 bootstrap 配置 <config_runtime_bootstrap>`
2. :ref:`本地磁盘文件系统  <config_runtime_local_disk>`
3. :ref:`本地磁盘文件系统  *override_subdirectory* <config_runtime_local_disk>`
4. :ref:`管理控制台覆盖 <config_runtime_admin>`

高层中的值会覆盖低层中相应的值。

.. _config_runtime_file_system:

文件系统布局
+++++++++++++

配置指南中的不同部分描述了可用的运行时配置。例如，:ref:`这儿 <config_cluster_manager_cluster_runtime>` 是针对上游集群的运行时设置。

在运行时中的每个‘.’指示了层次结构中的新目录，路径的末端部分是文件。文件的内容构成了运行时的值。当从一个文件读取数值时，空格和新行将会被忽略。

*numerator* 或 *denominator* 是预留的关键字，不能出现在任何目录中。

.. _config_runtime_bootstrap:

静态 bootstrap
++++++++++++++++

一个静态的基础运行时可以在 :ref:`bootstrap 配置 <envoy_v3_api_field_config.bootstrap.v3.Runtime.base>` 中通过一个:ref:`protobuf JSON 描述 <config_runtime_proto_json>` 来指定。

.. _config_runtime_local_disk:

本地磁盘文件系统
++++++++++++++++++

当在本地磁盘上实现了 :ref:`运行时虚拟文件系统 <config_runtime_file_system>` 时，它的根在 *symlink_root* + *subdirectory*。例如，*health_check.min_interval* 键将有用如下的全文件系统路径（使用符号链接）：

``/srv/runtime/current/envoy/health_check/min_interval``

.. _config_runtime_local_disk_overrides:

覆盖
~~~~~
任意数量的磁盘文件系统层可以在 :ref:`层级运行时 <envoy_v3_api_msg_config.bootstrap.v3.LayeredRuntime>` bootstrap 配置中进行覆盖。

在弃用的 :ref:`运行时 <envoy_v3_api_msg_config.bootstrap.v3.Runtime>` bootstrap 配置中，有一个可分辨的文件系统覆盖。假定文件夹  ``/srv/runtime/v1`` 指向存储在全局运行时配置中的实际文件系统路径。下面将会是一个针对运行时设置的典型配置：

* *symlink_root*: ``/srv/runtime/current``
* *subdirectory*: ``envoy``
* *override_subdirectory*: ``envoy_override``

其中 ``/srv/runtime/current`` 是一个链接至 ``/srv/runtime/v1`` 的符号链接。

.. _config_runtime_local_disk_service_cluster_subdirs:

特定于集群的子目录
~~~~~~~~~~~~~~~~~~~~

在弃用的 :ref:`运行时 <envoy_v3_api_msg_config.bootstrap.v3.Runtime>` bootstrap 配置中， *override_subdirectory* 和命令行选项中的 :option:`--service-cluster` 一起使用。假定 :option:`--service-cluster` 选项被设置为 ``my-cluster`` 。Envoy 会首先在下面的全文件系统路径中查找 *health_check.min_interval* 键：

``/srv/runtime/current/envoy_override/my-cluster/health_check/min_interval``

如果找到，则找到的的值值会对主查找路径中找到的任意值进行覆盖。这允许用户在全局默认值之上自定义单个集群的运行时值。

使用 :ref:`层级运行时 <envoy_v3_api_msg_config.bootstrap.v3.LayeredRuntime>` bootstrap 配置，在任意磁盘层上通过 :ref:`append_service_cluster <envoy_v3_api_field_config.bootstrap.v3.RuntimeLayer.DiskLayer.append_service_cluster>` 选项来在服务集群上进行定制化是可能的。

.. _config_runtime_symbolic_link_swap:

通过符号链接交换来更新运行时
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

更新任意运行时的值，需要分两步走。第一步，创建一个完整运行时树的硬拷贝，然后更新期望的运行时值。第二步，使用等同于如下的命令来实现符号链接根从旧运行时树到新运行时树的自动交换：

.. code-block:: console

  /srv/runtime:~$ ln -s /srv/runtime/v2 new && mv -Tf new current

文件系统数据的部署、垃圾回收等不在此文档的讨论范畴内。

.. _config_runtime_rtds:

运行时发现服务（RTDS）
++++++++++++++++++++++++

通过设定 :ref:`rtds_layer
<envoy_v3_api_field_config.bootstrap.v3.RuntimeLayer.rtds_layer>` 来指定和传送一个或多个运行时层。这将运行时层指向常规的 :ref:`xDS <xds_protocol>` 端点，能为给定的层订阅单个的 xDS 资源。那些层的资源类型是一种 :ref:`运行时消息 <envoy_v3_api_msg_service.runtime.v3.Runtime>`。

.. _config_runtime_admin:

管理控制台
+++++++++++++

可以在 :ref:`/runtime admin endpoint <operations_admin_interface_runtime>` 中查看值。可以在  :ref:`/runtime_modify admin endpoint <operations_admin_interface_runtime_modify>` 中对值进行修改和添加。如果没有配置运行时，就会使用空的 provider，这和使用内置于代码中的所有默认值是一样的，除了那些通过 `/runtime_modify` 来添加的值。

.. attention::

  使用 :ref:`/runtime_modify <operations_admin_interface_runtime_modify>` 端点的时候要小心。变化会立即生效。
  管理接口 :ref:`正确的配置安全 <operations_admin_interface_security>` 是非常重要的。

最多可指定一个管理层。如果指定了一个管理层缺失的非空 :ref:`层级运行时 <envoy_v3_api_msg_config.bootstrap.v3.LayeredRuntime>` bootstrap 配置，任何改变管理控制台的操作将会引发 503 响应。

.. _config_runtime_atomicity:

原子性
---------

在下列情形中，运行时将重新加载并生成一个新的快照：

* 当在符号链接根（symlink root）下检测到有文件移动操作发生或符号链接根发生变化时。
* 当添加或修改管理控制台覆盖时。

所有的运行时层在快照期间都会被评估。错误层会被忽略并从有效层里面剔除，可查看 :ref:`num_layers <runtime_stats>`。遍历符号链接根将花费一些很少的时间，因此如果期望真正的原子性，则运行时目录应该是不可变的且符号链接变化应该被用来对更新进行编排。

当检测到文件移动时，相同符号链接根的磁盘层将仅仅触发一次刷新。不完全相同的重叠符号链接根路径的磁盘层，在检测到文件移动时，可能会触发多次重新加载。

.. _config_runtime_proto_json:

Protobuf 和 JSON 描述
-----------------------

运行时 :ref:`文件系统 <config_runtime_file_system>` 可以在一个 proto3 消息中表示为  `google.protobuf.Struct
<https://developers.google.com/protocol-buffers/docs/reference/google.protobuf#google.protobuf.Struct>`_ ，且以如下规则为 JSON 对象建模：

* 点分隔符映射至树边缘。
* 变量叶子（整数、字符、布尔、双精度）均以他们自身的 JSON 类型来表示。 (integer, strings, booleans, doubles) are represented with their respective JSON type.
* :ref:`FractionalPercent <envoy_v3_api_msg_type.v3.FractionalPercent>` is represented with via its
  `canonical JSON encoding <https://developers.google.com/protocol-buffers/docs/proto3#json>`_.

关于 *health_check.min_interval* 键值在 YAML 文件中的示例如下：

.. code-block:: yaml

  health_check:
    min_interval: 5

.. note::

  从双精度解析而来的整数会向下取舍为最接近的整数。

.. _config_runtime_comments:

注释
-----

第一个字母以 ``#`` 开头的行被视为注释。

注释可被用来在对现有值提供上下文。注释在其他空文件中也很有用，以便在需要时保留占位符以便部署。

.. _config_runtime_deprecation:

使用运行时复写处理弃用特性
-----------------------------------------------

Envoy 运行时也是 Envoy 功能弃用流程的一部分。

如在 Envoy :repo:`重大变更策略 <CONTRIBUTING.md#breaking-change-policy>` 中所描述，Envoy 中的功能弃用分三个阶段：warn-by-default、fail-by-default 和 代码移除（code removal）。

在第一阶段，Envoy 在告警日志中会打印告警，表明功能要被弃用且会增加 :ref:`deprecated_feature_use <runtime_stats>` 运行时统计。鼓励用户去 :ref:`弃用 <deprecated>` 中查看如何迁移到新的代码路径，确保这能够适用于他们的用例。

在第二阶段，字段将被标记为 disallowed_by_default，默认情况下，使用此字段进行配置将会导致配置被拒绝。这种禁止模式可在运行时配置中通过将 envoy.deprecated_features:full_fieldname 或 envoy.deprecated_features:full_enum_value 设置为 true 来进行覆盖。例如，对于一个弃用字段 ``Foo.Bar.Eep`` ，将 ``envoy.deprecated_features:Foo.bar.Eep`` 设置为 ``true`` 。这有一个生产例子，使用静态运行时来允许 fail-by-default 字段，可查看这儿 :repo:`configs/using_deprecated_config.v2.yaml` 。**极其不鼓励** 去使用这些覆盖，所以使用时请注意，应尽早切换至新字段。Fatal-by-default 配置预示了旧代码路径的移除是迫在眉睫了。如果新代码路径中的任何缺陷或者功能问题能够提前被清除，而不是在代码移除之后，这对于Envoy 用户和 Envoy 贡献者来说都是非常好的！

.. _runtime_stats:

.. attention::

   版本高于 1.14.1 的 Envoy，不能够将整数值解析为运行时布尔值，这需要显示的设定为“true”或者“false”。如果错误的让诸如“0”这样的整数值代表“false”的话，会导致直接使用默认值。这一点对于 :ref:`弃用功能 <deprecated>` 的运行时覆盖尤为重要，因为这将会导致非预期的 Envoy 行为。

统计
-----

文件系统运行时提供者会在 *runtime.* 命名空间下发出一些统计。

.. csv-table::
  :header: 名称, 类型, 描述
  :widths: 1, 1, 2

  admin_overrides_active, Gauge, 如有任何管理覆盖处于激活状态则为 1 ，否则为 0。
  deprecated_feature_use, Counter, 使用弃用功能的次数。使用功能的详细信息将会以“从文件 Y 使用弃用选项‘X’”的格式被记录在告警日志中。
  deprecated_feature_seen_since_process_start, Gauge, 使用弃用功能的次数。这在热重启期间不会计入统计。
  load_error, Counter, 在任何层中导致错误的加载尝试总数
  load_success, Counter, 在所有层中加载尝试成功的总数
  num_keys, Gauge, 当前加载的键（key）数
  num_layers, Gauge, 当前活跃的层数（没有加载错误）
  override_dir_exists, Counter, 使用覆盖目录的负载总数
  override_dir_not_exists, Counter, 不使用覆盖目录的负载总数
