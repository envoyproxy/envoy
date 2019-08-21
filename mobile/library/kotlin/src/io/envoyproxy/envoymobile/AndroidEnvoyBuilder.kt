package io.envoyproxy.envoymobile

import android.content.Context
import io.envoyproxy.envoymobile.engine.AndroidEngineImpl

class AndroidEnvoyBuilder(
    context: Context
) : EnvoyBuilder() {

  init {
    addEngineType { AndroidEngineImpl(context) }
  }
}
