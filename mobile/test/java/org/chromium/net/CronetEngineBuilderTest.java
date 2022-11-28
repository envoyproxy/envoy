package org.chromium.net;

import static org.chromium.net.CronetProvider.PROVIDER_NAME_APP_PACKAGED;
import static org.chromium.net.CronetProvider.PROVIDER_NAME_FALLBACK;
import static org.chromium.net.testing.CronetTestRule.getContext;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SmallTest;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import org.chromium.net.testing.CronetTestRule;
import org.chromium.net.testing.CronetTestRule.OnlyRunNativeCronet;
import org.chromium.net.testing.Feature;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Tests {@link CronetEngine.Builder}.
 */
@RunWith(AndroidJUnit4.class)
public class CronetEngineBuilderTest {
  @Rule public final CronetTestRule mTestRule = new CronetTestRule();

  /**
   * Tests the comparison of two strings that contain versions.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  @OnlyRunNativeCronet
  public void testVersionComparison() {
    assertVersionIsHigher("22.44", "22.43.12");
    assertVersionIsLower("22.43.12", "022.124");
    assertVersionIsLower("22.99", "22.100");
    assertVersionIsHigher("22.100", "22.99");
    assertVersionIsEqual("11.2.33", "11.2.33");
    assertIllegalArgumentException(null, "1.2.3");
    assertIllegalArgumentException("1.2.3", null);
    assertIllegalArgumentException("1.2.3", "1.2.3x");
  }

  /**
   * Tests the correct ordering of the providers. The platform provider should be
   * the last in the list. Other providers should be ordered by placing providers
   * with the higher version first.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testProviderOrdering() {
    final CronetProvider[] availableProviders = new CronetProvider[] {
        createMockCronetProvider(PROVIDER_NAME_APP_PACKAGED, "99.77", true),
        createMockCronetProvider(PROVIDER_NAME_FALLBACK, "99.99", true),
        createMockCronetProvider("Some other provider", "99.88", true),
    };

    ArrayList<CronetProvider> providers = new ArrayList<>(Arrays.asList(availableProviders));
    List<CronetProvider> orderedProviders =
        CronetEngine.Builder.getEnabledCronetProviders(getContext(), providers);

    // Check the result
    assertEquals(availableProviders[2], orderedProviders.get(0));
    assertEquals(availableProviders[0], orderedProviders.get(1));
    assertEquals(availableProviders[1], orderedProviders.get(2));
  }

  /**
   * Tests that the providers that are disabled are not included in the list of available
   * providers when the provider is selected by the default selection logic.
   */
  @Test
  @SmallTest
  @Feature({"Cronet"})
  public void testThatDisabledProvidersAreExcluded() {
    final CronetProvider[] availableProviders = new CronetProvider[] {
        createMockCronetProvider(PROVIDER_NAME_FALLBACK, "99.99", true),
        createMockCronetProvider(PROVIDER_NAME_APP_PACKAGED, "99.77", true),
        createMockCronetProvider("Some other provider", "99.88", false),
    };

    ArrayList<CronetProvider> providers = new ArrayList<>(Arrays.asList(availableProviders));
    List<CronetProvider> orderedProviders =
        CronetEngine.Builder.getEnabledCronetProviders(getContext(), providers);

    assertEquals("Unexpected number of providers in the list", 2, orderedProviders.size());
    assertEquals(PROVIDER_NAME_APP_PACKAGED, orderedProviders.get(0).getName());
    assertEquals(PROVIDER_NAME_FALLBACK, orderedProviders.get(1).getName());
  }

  private void assertVersionIsHigher(String s1, String s2) {
    assertEquals(1, CronetEngine.Builder.compareVersions(s1, s2));
  }

  private void assertVersionIsLower(String s1, String s2) {
    assertEquals(-1, CronetEngine.Builder.compareVersions(s1, s2));
  }

  private void assertVersionIsEqual(String s1, String s2) {
    assertEquals(0, CronetEngine.Builder.compareVersions(s1, s2));
  }

  private void assertIllegalArgumentException(String s1, String s2) {
    try {
      CronetEngine.Builder.compareVersions(s1, s2);
    } catch (IllegalArgumentException e) {
      // Do nothing. It is expected.
      return;
    }
    fail("Expected IllegalArgumentException");
  }

  private static CronetProvider createMockCronetProvider(String mName, String mVersion,
                                                         boolean mEnabled) {
    CronetProvider mock = mock(CronetProvider.class);
    when(mock.getName()).thenReturn(mName);
    when(mock.getVersion()).thenReturn(mVersion);
    when(mock.isEnabled()).thenReturn(mEnabled);
    return mock;
  }
}
