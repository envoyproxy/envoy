package io.envoyproxy.envoymobile

import android.content.Context
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl

/** The engine builder to use to create Envoy engine on Android. */
class AndroidEngineBuilder(context: Context) : EngineBuilder() {
  init {
    addEngineType {
      AndroidEngineImpl(
        context,
        onEngineRunning,
        { level, msg -> logger?.let { it(LogLevel.from(level), msg) } },
        eventTracker,
        enableProxying
      )
    }
  }
}
