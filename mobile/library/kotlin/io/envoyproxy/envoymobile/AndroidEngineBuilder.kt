package io.envoyproxy.envoymobile

import android.content.Context
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl


/**
 * The engine builder to use to create Envoy engine on Android.
 */
class AndroidEngineBuilder @JvmOverloads constructor(
  context: Context,
  baseConfiguration: BaseConfiguration = Standard()
) : EngineBuilder(baseConfiguration) {
  init {
    addEngineType {
      AndroidEngineImpl(context, onEngineRunning, logger, eventTracker, enableProxying)
    }
  }
}
