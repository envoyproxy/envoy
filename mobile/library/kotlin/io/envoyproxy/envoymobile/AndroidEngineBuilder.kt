package io.envoyproxy.envoymobile

import android.app.Application
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl

class AndroidEngineBuilder @JvmOverloads constructor(
  application: Application,
  baseConfiguration: BaseConfiguration = Standard()
) : EngineBuilder(baseConfiguration) {
  init {
    addEngineType { AndroidEngineImpl(application) }
  }
}
