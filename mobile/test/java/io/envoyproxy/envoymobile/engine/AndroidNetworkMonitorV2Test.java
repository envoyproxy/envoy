package io.envoyproxy.envoymobile.engine;

import static com.google.common.truth.Truth.assertThat;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.times;
import static org.mockito.AdditionalMatchers.aryEq;

import static org.robolectric.Shadows.shadowOf;

import android.content.Context;
import android.content.Intent;
import android.Manifest;
import android.net.ConnectivityManager;
import android.net.NetworkCapabilities;
import android.net.Network;
import android.net.NetworkInfo;
import android.telephony.TelephonyManager;
import android.net.LinkProperties;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.rule.GrantPermissionRule;
import static androidx.test.core.app.ApplicationProvider.getApplicationContext;
import android.os.Build;
import android.os.Looper;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.shadows.ShadowNetwork;
import org.robolectric.shadows.ShadowNetworkInfo;
import org.robolectric.shadows.ShadowNetworkCapabilities;
import org.robolectric.annotation.Config;

import io.envoyproxy.envoymobile.engine.types.EnvoyNetworkType;
import io.envoyproxy.envoymobile.engine.types.EnvoyConnectionType;
import android.os.Looper;
import android.os.Handler;

/**
 * Tests functionality of AndroidNetworkMonitorV2
 */
@RunWith(RobolectricTestRunner.class)
// Individual tests may override this to test different Android SDK versions.
@Config(sdk = Build.VERSION_CODES.O)
public class AndroidNetworkMonitorV2Test {
  @Rule
  public GrantPermissionRule mRuntimePermissionRule =
      GrantPermissionRule.grant(Manifest.permission.ACCESS_NETWORK_STATE);

  private AndroidNetworkMonitorV2 androidNetworkMonitor;
  private ConnectivityManager connectivityManager;
  private NetworkCapabilities networkCapabilities;
  private final EnvoyEngine mockEnvoyEngine = mock(EnvoyEngine.class);

  @Before
  public void setUp() {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    AndroidNetworkMonitorV2.load(context, mockEnvoyEngine);
    androidNetworkMonitor = AndroidNetworkMonitorV2.getInstance();
    connectivityManager = androidNetworkMonitor.getConnectivityManager();
    networkCapabilities =
        connectivityManager.getNetworkCapabilities(connectivityManager.getActiveNetwork());
    int expectedNetworkCallbackSize = 2;
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
      expectedNetworkCallbackSize = 1;
    }
    assertThat(shadowOf(connectivityManager).getNetworkCallbacks())
        .hasSize(expectedNetworkCallbackSize);
  }

  @After
  public void tearDown() {
    AndroidNetworkMonitorV2.shutdown();
  }

  // Setup ShadowConnectivityManager with a new Network with given types and manually trigger a
  // series of network callbacks: onAvailable => onLinkPropertiesChanged => onCapabilitiesChanged
  // for on this network. Android platform guarantees to call onAvaialbe() before the other two.
  private Network triggerDefaultNetworkChange(int newTransportType, int newNetworkType,
                                              int newSubType) {
    // Setup the new network.
    NetworkInfo networkInfo =
        ShadowNetworkInfo.newInstance(NetworkInfo.DetailedState.CONNECTED, newNetworkType,
                                      newSubType, true, NetworkInfo.State.CONNECTED);
    shadowOf(connectivityManager).setActiveNetworkInfo(networkInfo);
    Network newNetwork = connectivityManager.getActiveNetwork();
    NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
    shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED);
    shadowOf(capabilities).addTransportType(newTransportType);
    shadowOf(connectivityManager).setNetworkCapabilities(newNetwork, capabilities);

    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      callback.onAvailable(newNetwork);
      LinkProperties link = new LinkProperties();
      callback.onLinkPropertiesChanged(newNetwork, link);
      callback.onCapabilitiesChanged(newNetwork, capabilities);
    });
    return newNetwork;
  }

  /**
   * Tests that isOnline() returns the correct result.
   */
  @Test
  public void testIsOnline() {
    shadowOf(networkCapabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    assertThat(androidNetworkMonitor.isOnline()).isTrue();

    shadowOf(networkCapabilities).removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    assertThat(androidNetworkMonitor.isOnline()).isFalse();
  }

  //=====================================================================================
  // TODO(fredyw): The ShadowConnectivityManager doesn't currently trigger
  // ConnectivityManager.NetworkCallback, so we have to call the callbacks manually. This
  // has been fixed in https://github.com/robolectric/robolectric/pull/9509 but it is
  // not available in the current Roboelectric Shadows framework that we use.
  //=====================================================================================
  @Test
  public void testOnDefaultNetworkAvailable() {
    Network network = ShadowNetwork.newInstance(0);
    NetworkInfo networkInfo = ShadowNetworkInfo.newInstance(NetworkInfo.DetailedState.CONNECTED,
                                                            ConnectivityManager.TYPE_WIFI, 0, true,
                                                            NetworkInfo.State.CONNECTED);
    shadowOf(connectivityManager).addNetwork(network, networkInfo);

    shadowOf(connectivityManager)
        .getNetworkCallbacks()
        .forEach(callback -> callback.onAvailable(network));

    verify(mockEnvoyEngine).onDefaultNetworkAvailable();
    verify(mockEnvoyEngine)
        .onNetworkConnect(EnvoyConnectionType.CONNECTION_WIFI, network.getNetworkHandle());
  }

  @Test
  public void testOnDefaultNetworkUnavailable() {
    Network network = ShadowNetwork.newInstance(0);
    shadowOf(connectivityManager)
        .getNetworkCallbacks()
        .forEach(callback -> callback.onLost(network));

    verify(mockEnvoyEngine).onDefaultNetworkUnavailable();
    verify(mockEnvoyEngine).onNetworkDisconnect(network.getNetworkHandle());
  }

  @Test
  public void testOnDefaultNetworkChangedWifi() {
    Network network = triggerDefaultNetworkChange(NetworkCapabilities.TRANSPORT_WIFI,
                                                  ConnectivityManager.TYPE_WIFI, 0);
    verify(mockEnvoyEngine)
        .onDefaultNetworkChangedV2(EnvoyConnectionType.CONNECTION_WIFI, network.getNetworkHandle());
    verify(mockEnvoyEngine, times(2))
        .onNetworkConnect(EnvoyConnectionType.CONNECTION_WIFI, network.getNetworkHandle());
    verify(mockEnvoyEngine).onDefaultNetworkAvailable();
  }

  @Test
  public void testOnDefaultNetworkChangedCell() {
    Network network = triggerDefaultNetworkChange(NetworkCapabilities.TRANSPORT_CELLULAR,
                                                  ConnectivityManager.TYPE_MOBILE,
                                                  TelephonyManager.NETWORK_TYPE_LTE);
    verify(mockEnvoyEngine)
        .onDefaultNetworkChangedV2(EnvoyConnectionType.CONNECTION_4G, network.getNetworkHandle());
    verify(mockEnvoyEngine, times(2))
        .onNetworkConnect(EnvoyConnectionType.CONNECTION_4G, network.getNetworkHandle());
    verify(mockEnvoyEngine).onDefaultNetworkAvailable();
  }

  @Test
  public void testOnDefaultNetworkChangedEthernet() {
    Network network = triggerDefaultNetworkChange(NetworkCapabilities.TRANSPORT_ETHERNET,
                                                  ConnectivityManager.TYPE_ETHERNET, 0);
    verify(mockEnvoyEngine)
        .onDefaultNetworkChangedV2(EnvoyConnectionType.CONNECTION_ETHERNET,
                                   network.getNetworkHandle());
    verify(mockEnvoyEngine, times(2))
        .onNetworkConnect(EnvoyConnectionType.CONNECTION_ETHERNET, network.getNetworkHandle());
    verify(mockEnvoyEngine).onDefaultNetworkAvailable();
  }

  @Test
  public void testOnCostChangedCallbackIsNotCalled() {
    Network activeNetwork = triggerDefaultNetworkChange(NetworkCapabilities.TRANSPORT_WIFI,
                                                        ConnectivityManager.TYPE_WIFI, 0);
    verify(mockEnvoyEngine)
        .onDefaultNetworkChangedV2(EnvoyConnectionType.CONNECTION_WIFI,
                                   activeNetwork.getNetworkHandle());

    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      NetworkCapabilities capabilities = connectivityManager.getNetworkCapabilities(activeNetwork);
      // Only change the cost of the default network.
      shadowOf(capabilities).removeCapability(NetworkCapabilities.NET_CAPABILITY_NOT_METERED);
      callback.onCapabilitiesChanged(activeNetwork, capabilities);
    });

    verify(mockEnvoyEngine, never()).onDefaultNetworkChanged(anyInt());
  }

  @Test
  public void testOnDefaultNetworkChangedVPN() {
    // Setup the active network.
    Network activeNetwork = triggerDefaultNetworkChange(NetworkCapabilities.TRANSPORT_WIFI,
                                                        ConnectivityManager.TYPE_WIFI, 0);
    verify(mockEnvoyEngine)
        .onDefaultNetworkChangedV2(EnvoyConnectionType.CONNECTION_WIFI,
                                   activeNetwork.getNetworkHandle());
    verify(mockEnvoyEngine, times(2))
        .onNetworkConnect(EnvoyConnectionType.CONNECTION_WIFI, activeNetwork.getNetworkHandle());

    // Setup the VPN network.
    NetworkInfo networkInfoVpn = ShadowNetworkInfo.newInstance(NetworkInfo.DetailedState.CONNECTED,
                                                               ConnectivityManager.TYPE_VPN, 0,
                                                               true, NetworkInfo.State.CONNECTED);
    Network vpnNetwork = ShadowNetwork.newInstance(2);
    shadowOf(connectivityManager).addNetwork(vpnNetwork, networkInfoVpn);
    NetworkCapabilities capabilities = ShadowNetworkCapabilities.newInstance();
    shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
    shadowOf(capabilities).addTransportType(NetworkCapabilities.TRANSPORT_VPN);
    shadowOf(connectivityManager).setNetworkCapabilities(vpnNetwork, capabilities);

    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      callback.onAvailable(vpnNetwork);
      LinkProperties link = new LinkProperties();
      callback.onLinkPropertiesChanged(vpnNetwork, link);
      callback.onCapabilitiesChanged(vpnNetwork, capabilities);
    });
    verify(mockEnvoyEngine, times(2))
        .onNetworkConnect(EnvoyConnectionType.CONNECTION_WIFI, vpnNetwork.getNetworkHandle());
    verify(mockEnvoyEngine, times(2))
        .onDefaultNetworkChangedV2(EnvoyConnectionType.CONNECTION_WIFI,
                                   vpnNetwork.getNetworkHandle());
    verify(mockEnvoyEngine)
        .purgeActiveNetworkList(aryEq(new long[] {vpnNetwork.getNetworkHandle()}));

    // Lost VPN network
    shadowOf(connectivityManager).getNetworkCallbacks().forEach(callback -> {
      callback.onLost(vpnNetwork);
    });
    // From DefaultNetworkCallback
    verify(mockEnvoyEngine).onDefaultNetworkChangedV2(EnvoyConnectionType.CONNECTION_NONE, -1);
    verify(mockEnvoyEngine).onDefaultNetworkUnavailable();
    // From MyNetworkCallback
    verify(mockEnvoyEngine).onNetworkDisconnect(vpnNetwork.getNetworkHandle());
    verify(mockEnvoyEngine, times(3))
        .onNetworkConnect(EnvoyConnectionType.CONNECTION_WIFI, activeNetwork.getNetworkHandle());
    verify(mockEnvoyEngine, times(2))
        .onDefaultNetworkChangedV2(EnvoyConnectionType.CONNECTION_WIFI,
                                   activeNetwork.getNetworkHandle());
  }

  // Tests that the broadcast receiver is triggered when the default network changes from cell to
  // WIFI on Android M.
  @Test
  @Config(sdk = Build.VERSION_CODES.M)
  public void testBroadcastReceiver() {
    Network activeNetwork = triggerDefaultNetworkChange(NetworkCapabilities.TRANSPORT_WIFI,
                                                        ConnectivityManager.TYPE_WIFI, 0);
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    Intent intent = new Intent(ConnectivityManager.CONNECTIVITY_ACTION);
    context.sendBroadcast(intent);
    // Robolectric doesn't seem to run the receiver in the main looper, so we need to idle it.
    shadowOf(Looper.getMainLooper()).idle();
    verify(mockEnvoyEngine)
        .onDefaultNetworkChangedV2(EnvoyConnectionType.CONNECTION_WIFI,
                                   activeNetwork.getNetworkHandle());
  }
}
