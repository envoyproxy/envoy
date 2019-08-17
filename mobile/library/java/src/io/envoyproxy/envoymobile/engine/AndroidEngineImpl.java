package io.envoyproxy.envoymobile.engine;

import android.content.Context;

public class AndroidEngineImpl extends EnvoyEngineImpl {

  public AndroidEngineImpl(Context context) {
    super();
    AndroidJniLibrary.load(context);
  }
}
