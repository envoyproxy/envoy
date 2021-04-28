package io.envoyproxy.envoymobile

import android.content.Context
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl

class AndroidEngineBuilder @JvmOverloads constructor(
  context: Context,
  baseConfiguration: BaseConfiguration = Standard()
) : EngineBuilder(baseConfiguration) {
  init {
    addEngineType { AndroidEngineImpl(context, onEngineRunning, logger) }
  }
}
