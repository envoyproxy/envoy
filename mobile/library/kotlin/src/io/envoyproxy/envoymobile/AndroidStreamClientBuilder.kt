package io.envoyproxy.envoymobile

import android.app.Application
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl

class AndroidStreamClientBuilder @JvmOverloads constructor (
  application: Application,
  baseConfiguration: BaseConfiguration = Standard()
) : StreamClientBuilder(baseConfiguration) {
  init {
    addEngineType { AndroidEngineImpl(application) }
  }
}
