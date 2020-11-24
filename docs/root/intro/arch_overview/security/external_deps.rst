.. _arch_overview_external_deps:

外部依赖
=====================

下面列举了可能被链接到 Envoy 二进制文件中的外部依赖项，排除了那些只在 CI 或开发者工具中使用的依赖项。

数据平面（核心）
-----------------

.. include:: external_dep_dataplane_core.rst

数据平面（扩展）
-----------------------

.. include:: external_dep_dataplane_ext.rst

控制平面
-------------

.. include:: external_dep_controlplane.rst

可观测性（核心）
--------------------

.. include:: external_dep_observability_core.rst

可观测性（扩展）
--------------------------

.. include:: external_dep_observability_ext.rst

仅测试
---------

.. include:: external_dep_test_only.rst

构建
-----

.. include:: external_dep_build.rst

其他
-------------

.. include:: external_dep_other.rst
