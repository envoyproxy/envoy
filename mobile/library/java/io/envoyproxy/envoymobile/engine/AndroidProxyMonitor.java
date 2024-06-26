package io.envoyproxy.envoymobile.engine;

import android.annotation.TargetApi;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.ConnectivityManager;
import android.net.Proxy;
import android.net.ProxyInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;

@TargetApi(Build.VERSION_CODES.LOLLIPOP)
class AndroidProxyMonitor extends BroadcastReceiver {
  static volatile AndroidProxyMonitor instance = null;
  private final ConnectivityManager connectivityManager;
  private final EnvoyEngine envoyEngine;

  static void load(Context context, EnvoyEngine envoyEngine) {
    if (instance != null) {
      return;
    }

    synchronized (AndroidProxyMonitor.class) {
      if (instance != null) {
        return;
      }
      instance = new AndroidProxyMonitor(context, envoyEngine);
    }
  }

  private AndroidProxyMonitor(Context context, EnvoyEngine envoyEngine) {
    this.envoyEngine = envoyEngine;
    this.connectivityManager =
        (ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE);
    // PROXY_INFO is not guaranteed to be a sticky intent so we need to trigger
    // a manual poll of proxy settings. Proxy settings received as a result of PROXY_INFO
    // intent updates should take precedence/override locally polled settings,
    // hence we trigger a manual proxy settings poll before we subscribe to PROXY_INFO intent
    // updates.
    handleProxyChange(null);
    registerReceiver(context);
  }

  private void registerReceiver(Context context) {
    context.getApplicationContext().registerReceiver(this, new IntentFilter() {
      { addAction(Proxy.PROXY_CHANGE_ACTION); }
    });
  }

  @Override
  public void onReceive(Context context, Intent intent) {
    handleProxyChange(intent);
  }

  private void handleProxyChange(final Intent intent) {
    ProxyInfo info = this.extractProxyInfo(intent);

    if (info == null) {
      envoyEngine.setProxySettings("", 0);
    } else {
      envoyEngine.setProxySettings(info.getHost(), info.getPort());
    }
  }

  private ProxyInfo extractProxyInfo(final Intent intent) {
    ProxyInfo info = connectivityManager.getDefaultProxy();
    if (info == null) {
      return null;
    }

    // If a proxy is configured using the PAC file use
    // Android's injected localhost HTTP proxy.
    //
    // Android's injected localhost proxy can be accessed using a proxy host
    // equal to `localhost` and a proxy port retrieved from intent's 'extras'.
    // We cannot take a proxy port from the ProxyInfo object that's exposed by
    // the connectivity manager as it's always equal to -1 for cases when PAC
    // proxy is configured.
    //
    // See https://github.com/envoyproxy/envoy-mobile/issues/2531 for more details.
    if (!Uri.EMPTY.equals(info.getPacFileUrl())) {
      if (intent == null) {
        // PAC proxies are supported only when Intent is present
        return null;
      }

      Bundle extras = intent.getExtras();
      if (extras == null) {
        return null;
      }

      info = (ProxyInfo)extras.get("android.intent.extra.PROXY_INFO");
    }

    return info;
  }
}
