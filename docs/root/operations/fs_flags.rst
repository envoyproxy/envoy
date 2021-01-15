.. _operations_file_system_flags:

文件系统标志
==============

Envoy 支持在启动时改变状态的文件系统“标志”。在有需要的时候，这被用来在重启之间保持更改。标志文件应该被放置在指定的目录下，此目录在 :ref:`flags_path <envoy_v3_api_field_config.bootstrap.v3.Bootstrap.flags_path>` 选项中进行配置。目前支持的标志文件有：

排空
  如果此文件存在，Envoy 将在健康检查失败模式下启动，类似于 :http:post:`/healthcheck/fail` 命令被执行后的情况。
