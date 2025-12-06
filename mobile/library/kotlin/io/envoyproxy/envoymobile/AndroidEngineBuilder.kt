package io.envoyproxy.envoymobile

import android.content.Context
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl

/** The engine builder to use to create Envoy engine on Android. */
class AndroidEngineBuilder(context: Context) : EngineBuilder() {
  private var useV2NetworkMonitor = false

  fun setUseV2NetworkMonitor(useV2NetworkMonitor: Boolean): AndroidEngineBuilder {
    this.useV2NetworkMonitor = useV2NetworkMonitor
    return this
  }

  init {
    addEngineType {
      AndroidEngineImpl(
        context,
        onEngineRunning,
        { level, msg -> logger?.let { it(LogLevel.from(level), msg) } },
        eventTracker,
        enableProxying,
        /*useNetworkChangeEvent*/ false,
        /*disableDnsRefreshOnNetworkChange*/ false,
        useV2NetworkMonitor
      )
    }
  }
}
