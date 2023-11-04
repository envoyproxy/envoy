## Fine-Grain Logger: Flexible Granularity Log Control in Envoy
### **Note**: this was renamed from **Fancy** Logger as the name was opaque and didn't reflect its fine grain logging capabilities.

### Overview
Fine-Grain Logger is a logger with finer grained log level control and runtime logger update using administration interface. Compared to the existing logger in Envoy, Fine-Grain Logger provides file level control which can be easily extended to finer grained logger with function or line level control, and it is completely automatic and never requires developers to explicitly specify the logging component. Besides, it has a comparable speed as Envoy's logger.

### Basic Usage
The basic usage of Fine-Grain Logger is to explicitly call its macros:
```
  FINE_GRAIN_LOG(info, "Hello world! Here's a line of fine-grain log!");
  FINE_GRAIN_LOG(error, "FineGrainLog Error! Here's the second message!");
```
If the level of log message is higher than that of the file, macros above will print messages with the file name like this:
```
[2020-07-29 22:27:02.594][15][error][test/common/common/log_macros_test.cc:149] FineGrainLog Error! Here\'s the second message!
```
More macros with connection and stream information:
```
  NiceMock<Network::MockConnection> connection_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> stream_;
  FINE_GRAIN_CONN_LOG(warn, "Fake info {} of connection", connection_, 1);
  FINE_GRAIN_STREAM_LOG(warn, "Fake warning {} of stream", stream_, 1);
```
To flush a logger, `FINE_GRAIN_FLUSH_LOG()` can be used.

### Enable Fine-Grain Logger using Command Line Option
A command line option is provided to enable Fine-Grain Logger: `--enable-fine-grain-logging`. It enables Fine-Grain Logger for Envoy, i.e. replaces most Envoy's log macros (`ENVOY_LOG, ENVOY_FLUSH_LOG, ENVOY_CONN_LOG, ENVOY_STREAM_LOG`) with corresponding Fine-Grain Logger's macros.

If Fine-Grain Logger is enabled, the default log format is `"[%Y-%m-%d %T.%e][%t][%l] [%g:%#] %v"`, where the logger name is omitted compared to Envoy's default as it's the same as file name. The default log level is info, if not specified by user of any logging context.

Note that Envoy's logger can still be used in Fine-Grain mode. These macros are not replaced: `GET_MISC_LOGGER, ENVOY_LOG_MISC, ENVOY_LOGGER, ENVOY_LOG_TO_LOGGER, ENVOY_CONN_LOG_TO_LOGGER, ENVOY_STREAM_LOG_TO_LOGGER`. For example, `ENVOY_LOG_LOGGER(ENVOY_LOGGER(), LEVEL, ...)` is equivalent to `ENVOY_LOG` in Envoy mode.

If Fine-Grain Logger is not enabled, existing Envoy's logger is used. In this mode, basic macros like `FINE_GRAIN_LOG` can be used but the main part of `ENVOY_LOG` will keep the same. One limitation is that logger update in admin page is not supported by default as it detects Envoy mode. The reason is: Envoy mode is designed only to be back compatible. To address it, developers can use `Logger::Context::enableFineGrainLogger()` to manually enable Fine-Grain Logger.

### Runtime Update
Runtime update of Fine-Grain Logger is supported with administration interface, i.e. admin page, and Fine-Grain mode needs to be enabled to use it. Same as Envoy's logger, the following functionalities are provided:

1. `POST /logging`: List all names (i.e. file paths) of all active loggers and their levels;
2. `POST /logging?<file_path>=<level>`: Given the current file path, change the log level of the file;
3. `POST /logging?paths=file_path1:level1,file_path2:level2...`: Change log level of multiple file paths at once;
4. `POST /logging?level=<level>`: Change levels of all loggers.

Users can view and change the log level by file granularity at runtime through the admin page.

Envoy admin `/logging` supports the file basename, glob ``*`` and ``?`` match for the fine-grain loggers update as well. For example, the following loggers are active in Envoy right now, and 0 means trace level.

```
  source/server/admin/admin_filter.cc: 0
  source/common/event/dispatcher_impl.cc: 0
  source/common/network/tcp_listener_impl.cc: 0
  source/common/network/udp_listener_impl.cc: 0
```

  1. ``POST /logging?paths=source/common/event/dispatcher_impl.cc:debug`` will make the level of ``source/common/event/dispatcher_impl.cc`` be debug.
  2. ``POST /logging?admin_filter=info`` will make the level of ``source/server/admin/admin_filter.cc`` be info, and other unmatched loggers will be the default trace.
  3. ``POST /logging?paths=source/common*:warning`` will make the level of ``source/common/event/dispatcher_impl.cc:``, ``source/common/network/tcp_listener_impl.cc`` be warning.
    Other unmatched loggers will be the default trace, e.g., `admin_filter.cc`, even it was updated to info from the previous post update.
  4. ``POST /logging?paths=???_listener_impl:info`` will make the level of ``source/common/network/tcp_listener_impl.cc``, ``source/common/network/udp_listener_impl.cc`` be info.
  5. ``POST /logging?paths=???_listener_impl:info,tcp_listener_impl:warning``, the level of ``source/common/network/tcp_listener_impl.cc`` will be info, since the first match will take effect.
  6. ``POST /logging?level=info`` will change the default verbosity level to info. All the unmatched loggers in the following update will be this default level.


### Implementation Details
Fine-Grain Logger can be divided into two parts:
1. Core part: file level logger without explicit inheriting `Envoy::Logger::Loggable`;
2. Hook part: control interfaces, such as command line and admin page.

#### Core Part
The core of Fine-Grain Logger is implemented in `class FineGrainLogContext`, which is a singleton class. Filenames (i.e. keys) and logger pointers are stored in `FineGrainLogContext::fine_grain_log_map_`. There are several code paths when `FINE_GRAIN_LOG` is called.

1. Slow path: if `FINE_GRAIN_LOG` is first called in a file, `FineGrainLogContext::initFineGrainLogger(key, local_logger_ptr)` is called to create a new logger globally and store the `<key, global_logger_ptr>` pair in `fine_grain_log_map_`. Then local logger pointer is updated and will never be changed.
2. Medium path: if `FINE_GRAIN_LOG` is first called at the call site but not in the file, `FineGrainLogContext::initFineGrainLogger(key, local_logger_ptr)` is still called but local logger pointer is quickly set to the global logger, as there is already global record of the given filename.
3. Fast path: if `FINE_GRAIN_LOG` is called after first call at the site, the log is directly printed using local logger pointer.

#### Hook Part
Fine-Grain Logger provides control interfaces through command line and admin page.

To pass the arguments such as log format and default log level to Fine-Grain Logger, `fine_grain_log_format` and `fine_grain_default_level` are added in `class Context` and they are updated when a new logging context is activated. `getFineGrainLogContext().setDefaultFineGrainLogLevelFormat(level, format)` is called in `Context::activate()` to set log format and update loggers' previous default level to the new level.

To support the runtime update in admin page, log handler in admin page uses `getFineGrainLogContext().listFineGrainLoggers()` to show all Fine-Grain Loggers, `getFineGrainLogContext().setFineGrainLogger(name, level)` to set a specific logger, `getFineGrainLogContext().setAllFineGrainLoggers(level)` to set all loggers, and `getFineGrainLogContext().updateVerbositySetting(glob_levels)` to set glob matched loggers.

