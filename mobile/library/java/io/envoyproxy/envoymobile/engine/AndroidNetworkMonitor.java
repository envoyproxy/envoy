package io.envoyproxy.envoymobile.engine;

import io.envoyproxy.envoymobile.engine.types.EnvoyNetworkType;

import android.Manifest;
import android.annotation.TargetApi;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.net.ConnectivityManager;
import android.net.ConnectivityManager.NetworkCallback;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkInfo;
import android.net.NetworkRequest;
import android.os.Build;
import androidx.core.content.ContextCompat;

import java.util.Collections;

/**
 * This class makes use of some deprecated APIs, but it's only current purpose is to attempt to
 * distill some notion of a preferred network from the OS, upon which we can assume new sockets will
 * be opened.
 */
@TargetApi(Build.VERSION_CODES.LOLLIPOP)
public class AndroidNetworkMonitor extends BroadcastReceiver {
  private static final String PERMISSION_DENIED_STATS_ELEMENT =
      "android_permissions.network_state_denied";
  private static final int ENVOY_NET_GENERIC = 0;
  private static final int ENVOY_NET_WWAN = 1;
  private static final int ENVOY_NET_WLAN = 2;

  private static volatile AndroidNetworkMonitor instance = null;

  private int previousNetworkType = ConnectivityManager.TYPE_DUMMY;
  private EnvoyEngine envoyEngine;
  private ConnectivityManager connectivityManager;
  private NetworkCallback networkCallback;
  private NetworkRequest networkRequest;

  public static void load(Context context, EnvoyEngine envoyEngine) {
    if (instance != null) {
      return;
    }

    synchronized (AndroidNetworkMonitor.class) {
      if (instance != null) {
        return;
      }
      instance = new AndroidNetworkMonitor(context, envoyEngine);
    }
  }

  private AndroidNetworkMonitor(Context context, EnvoyEngine envoyEngine) {
    int permission =
        ContextCompat.checkSelfPermission(context, Manifest.permission.ACCESS_NETWORK_STATE);
    if (permission == PackageManager.PERMISSION_DENIED) {
      try {
        envoyEngine.recordCounterInc(PERMISSION_DENIED_STATS_ELEMENT, Collections.emptyMap(), 1);
      } catch (Throwable t) {
        // no-op if this errors out and return
      }
      return;
    }

    this.envoyEngine = envoyEngine;

    connectivityManager =
        (ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE);
    networkRequest = new NetworkRequest.Builder()
                         .addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                         .build();

    networkCallback = new NetworkCallback() {
      @Override
      public void onAvailable(Network network) {
        handleNetworkChange();
      }
      @Override
      public void onCapabilitiesChanged(Network network, NetworkCapabilities networkCapabilities) {
        handleNetworkChange();
      }
      @Override
      public void onLosing(Network network, int maxMsToLive) {
        handleNetworkChange();
      }
      @Override
      public void onLost(final Network network) {
        handleNetworkChange();
      }
    };

    try {
      connectivityManager.registerNetworkCallback(networkRequest, networkCallback);

      context.registerReceiver(this, new IntentFilter() {
        { addAction(ConnectivityManager.CONNECTIVITY_ACTION); }
      });
    } catch (Throwable t) {
      // no-op
    }
  }

  @Override
  public void onReceive(Context context, Intent intent) {
    handleNetworkChange();
  }

  private void handleNetworkChange() {
    NetworkInfo networkInfo = connectivityManager.getActiveNetworkInfo();
    int networkType = networkInfo == null ? -1 : networkInfo.getType();
    if (networkType == previousNetworkType) {
      return;
    }
    previousNetworkType = networkType;

    switch (networkType) {
    case ConnectivityManager.TYPE_MOBILE:
      envoyEngine.setPreferredNetwork(EnvoyNetworkType.ENVOY_NETWORK_TYPE_WWAN);
      return;
    case ConnectivityManager.TYPE_WIFI:
      envoyEngine.setPreferredNetwork(EnvoyNetworkType.ENVOY_NETWORK_TYPE_WLAN);
      return;
    default:
      envoyEngine.setPreferredNetwork(EnvoyNetworkType.ENVOY_NETWORK_TYPE_GENERIC);
    }
  }
}
