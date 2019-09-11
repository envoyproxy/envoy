package io.envoyproxy.envoymobile

import io.envoyproxy.envoymobile.engine.EnvoyConfiguration
import io.envoyproxy.envoymobile.engine.EnvoyEngine
import java.nio.ByteBuffer

/**
 * Available logging levels for an Envoy instance. Note some levels may be compiled out.
 */
enum class LogLevel(internal val level: String) {
  TRACE("trace"),
  DEBUG("debug"),
  INFO("info"),
  WARN("warn"),
  ERROR("error"),
  CRITICAL("critical"),
  OFF("off");
}

/**
 * Wrapper class that allows for easy calling of Envoy's JNI interface in native Java.
 */
class Envoy private constructor(
    private val engine: EnvoyEngine,
    internal val envoyConfiguration: EnvoyConfiguration?,
    internal val configurationYAML: String?,
    internal val logLevel: LogLevel
) : HTTPClient {

  constructor(engine: EnvoyEngine, envoyConfiguration: EnvoyConfiguration, logLevel: LogLevel = LogLevel.INFO) : this(engine, envoyConfiguration, null, logLevel)
  constructor(engine: EnvoyEngine, configurationYAML: String, logLevel: LogLevel = LogLevel.INFO) : this(engine, null, configurationYAML, logLevel)

  // Dedicated thread for running this instance of Envoy.
  private val runner: Thread = Thread(ThreadGroup("Envoy"), Runnable {
    if (envoyConfiguration == null) {
      engine.runWithConfig(configurationYAML, logLevel.level)
    }else {
      engine.runWithConfig(envoyConfiguration, logLevel.level)
    }
  })

  /**
   * Create a new Envoy instance. The Envoy runner Thread is started as part of instance
   * initialization with the configuration provided. If the Envoy native library and its
   * dependencies haven't been loaded and initialized yet, this will happen lazily when
   * the first instance is created.
   */
  init {
    runner.start()
  }

  /**
   * Returns whether the Envoy instance is currently active and running.
   */
  fun isRunning(): Boolean {
    val state = runner.state
    return state != Thread.State.NEW && state != Thread.State.TERMINATED
  }

  /**
   * Returns whether the Envoy instance is terminated.
   */
  fun isTerminated(): Boolean {
    return runner.state == Thread.State.TERMINATED
  }

  override fun send(request: Request, responseHandler: ResponseHandler): StreamEmitter {
    val stream = engine.startStream(responseHandler.underlyingCallbacks)
    stream.sendHeaders(request.outboundHeaders(), false)
    return EnvoyStreamEmitter(stream)
  }

  override fun send(request: Request, body: ByteBuffer?, trailers: Map<String, List<String>>, responseHandler: ResponseHandler): CancelableStream {
    val stream = send(request, responseHandler)
    if (body != null) {
      stream.sendData(body)
    }
    stream.close(trailers)
    return stream
  }

  override fun send(request: Request, body: ByteBuffer?, responseHandler: ResponseHandler): CancelableStream {
    return send(request, body, emptyMap(), responseHandler)
  }
}
