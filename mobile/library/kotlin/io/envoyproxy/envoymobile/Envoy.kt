package io.envoyproxy.envoymobile

import android.content.Context
import io.envoyproxy.envoymobile.engine.EnvoyEngine

/**
 * Wrapper class that allows for easy calling of Envoy's JNI interface in native Java.
 */
class Envoy @JvmOverloads constructor(
    context: Context,
    config: String,
    logLevel: String = "info"
) {

  // Dedicated thread for running this instance of Envoy.
  private val runner: Thread

  /**
   * Create a new Envoy instance. The Envoy runner Thread is started as part of instance
   * initialization with the configuration provided. If the Envoy native library and its
   * dependencies haven't been loaded and initialized yet, this will happen lazily when
   * the first instance is created.
   */
  init {
    // Lazily initialize Envoy and its dependencies, if necessary.
    load(context)

    runner = Thread(Runnable {
      EnvoyEngine.run(config.trim(), logLevel)
    })

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

  companion object {
    @JvmStatic
    fun load(context: Context) {
      EnvoyEngine.load(context)
    }
  }
}
