package io.envoyproxy.envoymobile.utilities;

import static org.junit.Assert.*;
import static org.mockito.Mockito.mock;
import static org.robolectric.Shadows.shadowOf;

import io.envoyproxy.envoymobile.engine.JniLibrary;
import io.envoyproxy.envoymobile.engine.EnvoyEngine;
import io.envoyproxy.envoymobile.engine.AndroidNetworkMonitorV2;
import io.envoyproxy.envoymobile.engine.types.EnvoyConnectionType;

import java.nio.charset.StandardCharsets;

import android.content.Context;
import android.Manifest;
import android.net.ConnectivityManager;
import android.net.NetworkCapabilities;
import android.net.Network;
import android.net.NetworkInfo;

import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.rule.GrantPermissionRule;

import org.junit.After;
import org.junit.Before;
import org.junit.BeforeClass;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.robolectric.RobolectricTestRunner;
import org.robolectric.shadows.ShadowNetworkInfo;
import org.robolectric.shadows.ShadowNetworkCapabilities;

@RunWith(RobolectricTestRunner.class)
public final class AndroidNetworkTest {
  @Rule
  public GrantPermissionRule mRuntimePermissionRule =
      GrantPermissionRule.grant(Manifest.permission.ACCESS_NETWORK_STATE);

  private AndroidNetworkMonitorV2 mAndroidNetworkMonitor;
  private ConnectivityManager mConnectivityManager;
  private final EnvoyEngine mMockEnvoyEngine = mock(EnvoyEngine.class);

  @BeforeClass
  public static void beforeClass() {
    JniLibrary.loadTestLibrary();
  }

  @Before
  public void setUp() throws Exception {
    Context context = InstrumentationRegistry.getInstrumentation().getTargetContext();
    if (ContextUtils.getApplicationContext() == null) {
      ContextUtils.initApplicationContext(context.getApplicationContext());
    }
    AndroidNetworkMonitorV2.load(context, mMockEnvoyEngine);
    mAndroidNetworkMonitor = AndroidNetworkMonitorV2.getInstance();
    mConnectivityManager = mAndroidNetworkMonitor.getConnectivityManager();
  }

  @After
  public void tearDown() throws Exception {
    AndroidNetworkMonitorV2.shutdown();
  }

  @Test
  public void testGetDefaultNetworkHandle() {
    Network activeNetwork = mConnectivityManager.getActiveNetwork();
    long networkHandle = JniLibrary.callGetDefaultNetworkHandleFromNative();
    assertEquals(activeNetwork.getNetworkHandle(), networkHandle);

    NetworkInfo wifiNetworkInfo = ShadowNetworkInfo.newInstance(NetworkInfo.DetailedState.CONNECTED,
                                                                ConnectivityManager.TYPE_WIFI, 0,
                                                                true, NetworkInfo.State.CONNECTED);
    shadowOf(mConnectivityManager).setActiveNetworkInfo(wifiNetworkInfo);
    Network wifiNetwork = mConnectivityManager.getActiveNetwork();
    long wifiNetworkHandle = JniLibrary.callGetDefaultNetworkHandleFromNative();
    assertEquals(wifiNetwork.getNetworkHandle(), wifiNetworkHandle);
  }

  @Test
  public void testGetAllConnectedNetworks() {
    // Make all networks connected to the internet.
    Network[] networks = mConnectivityManager.getAllNetworks();
    for (Network network : networks) {
      NetworkCapabilities capabilities = mConnectivityManager.getNetworkCapabilities(network);
      shadowOf(capabilities).addCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET);
      NetworkInfo netInfo = mConnectivityManager.getNetworkInfo(network);
      shadowOf(netInfo).setConnectionStatus(NetworkInfo.State.CONNECTED);
    }
    long[][] networkArray = JniLibrary.callGetAllConnectedNetworksFromNative();
    assertEquals(networks.length, networkArray.length);
    // The ShadowConnectivityManager should have 2 networks cached, one default WIFI network and
    // another cellular one.
    Network cellNetwork = null;
    for (int i = 0; i < networks.length; ++i) {
      assertEquals(networks[i].getNetworkHandle(), networkArray[i][0]);
      NetworkCapabilities capabilities = mConnectivityManager.getNetworkCapabilities(networks[i]);
      if (capabilities.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
        assertEquals(EnvoyConnectionType.CONNECTION_WIFI.getValue(), networkArray[i][1]);
      } else {
        cellNetwork = networks[i];
        assertTrue(capabilities.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR));
        assertEquals(EnvoyConnectionType.CONNECTION_2G.getValue(), networkArray[i][1]);
      }
    }

    assertNotNull(cellNetwork);
    shadowOf(mConnectivityManager).removeNetwork(cellNetwork);
    networkArray = JniLibrary.callGetAllConnectedNetworksFromNative();
    assertEquals(1, networkArray.length);
    assertEquals(EnvoyConnectionType.CONNECTION_WIFI.getValue(), networkArray[0][1]);
  }
}
