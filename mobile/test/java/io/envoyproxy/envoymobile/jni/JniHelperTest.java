package io.envoyproxy.envoymobile.jni;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class JniHelperTest {
  public JniHelperTest() { System.loadLibrary("envoy_jni_helper_test"); }

  //================================================================================
  // Native methods for testing.
  //================================================================================
  public static native void getMethodId(Class<?> clazz, String name, String signature);
  public static native void getStaticMethodId(Class<?> clazz, String name, String signature);
  public static native Class<?> findClass(String className);
  public static native Class<?> getObjectClass(Object object);
  public static native Object newObject(Class<?> clazz, String name, String signature);
  public static native void throwNew(String className, String message);
  public static native int getArrayLength(int[] array);
  public static native byte[] newByteArray(int length);
  public static native char[] newCharArray(int length);
  public static native short[] newShortArray(int length);
  public static native int[] newIntArray(int length);
  public static native long[] newLongArray(int length);
  public static native float[] newFloatArray(int length);
  public static native double[] newDoubleArray(int length);
  public static native boolean[] newBooleanArray(int length);
  public static native Object[] newObjectArray(int length, Class<?> elementClass,
                                               Object initialElement);
  public static native Object getObjectArrayElement(Object[] array, int index);
  public static native void setObjectArrayElement(Object[] array, int index, Object value);
  public static native byte callByteMethod(Class<?> clazz, Object instance, String name,
                                           String signature);
  public static native char callCharMethod(Class<?> clazz, Object instance, String name,
                                           String signature);
  public static native short callShortMethod(Class<?> clazz, Object instance, String name,
                                             String signature);
  public static native int callIntMethod(Class<?> clazz, Object instance, String name,
                                         String signature);
  public static native long callLongMethod(Class<?> clazz, Object instance, String name,
                                           String signature);
  public static native float callFloatMethod(Class<?> clazz, Object instance, String name,
                                             String signature);
  public static native double callDoubleMethod(Class<?> clazz, Object instance, String name,
                                               String signature);
  public static native boolean callBooleanMethod(Class<?> clazz, Object instance, String name,
                                                 String signature);
  public static native void callVoidMethod(Class<?> clazz, Object instance, String name,
                                           String signature);
  public static native Object callObjectMethod(Class<?> clazz, Object instance, String name,
                                               String signature);
  public static native byte callStaticByteMethod(Class<?> clazz, String name, String signature);
  public static native char callStaticCharMethod(Class<?> clazz, String name, String signature);
  public static native short callStaticShortMethod(Class<?> clazz, String name, String signature);
  public static native int callStaticIntMethod(Class<?> clazz, String name, String signature);
  public static native long callStaticLongMethod(Class<?> clazz, String name, String signature);
  public static native float callStaticFloatMethod(Class<?> clazz, String name, String signature);
  public static native double callStaticDoubleMethod(Class<?> clazz, String name, String signature);
  public static native boolean callStaticBooleanMethod(Class<?> clazz, String name,
                                                       String signature);
  public static native void callStaticVoidMethod(Class<?> clazz, String name, String signature);
  public static native Object callStaticObjectMethod(Class<?> clazz, String name, String signature);

  //================================================================================
  // Object methods used for Call<Type>Method tests.
  //================================================================================
  public byte byteMethod() { return 1; }
  public char charMethod() { return 'a'; }
  public short shortMethod() { return 1; }
  public int intMethod() { return 1; }
  public long longMethod() { return 1; }
  public float floatMethod() { return 3.14f; }
  public double doubleMethod() { return 3.14; }
  public boolean booleanMethod() { return true; }
  public void voidMethod() {}
  public String objectMethod() { return "Hello"; }

  //================================================================================
  // Static methods used for CallStatic<Type>Method tests.
  //================================================================================
  public static byte staticByteMethod() { return 1; }
  public static char staticCharMethod() { return 'a'; }
  public static short staticShortMethod() { return 1; }
  public static int staticIntMethod() { return 1; }
  public static long staticLongMethod() { return 1; }
  public static float staticFloatMethod() { return 3.14f; }
  public static double staticDoubleMethod() { return 3.14; }
  public static boolean staticBooleanMethod() { return true; }
  public static void staticVoidMethod() {}
  public static String staticObjectMethod() { return "Hello"; }

  static class Foo {}

  @Test
  public void testMethodId() {
    getMethodId(Foo.class, "<init>", "()V");
  }

  @Test
  public void testStaticMethodId() {
    getStaticMethodId(JniHelperTest.class, "staticVoidMethod", "()V");
  }

  @Test
  public void testFindClass() {
    assertThat(findClass("java/lang/Exception")).isEqualTo(Exception.class);
  }

  @Test
  public void testGetObjectClass() {
    String s = "Hello";
    assertThat(getObjectClass(s)).isEqualTo(String.class);
  }

  @Test
  public void testNewObject() {
    assertThat(newObject(Foo.class, "<init>", "()V")).isInstanceOf(Foo.class);
  }

  @Test
  public void testThrowNew() {
    assertThatThrownBy(() -> throwNew("java/lang/RuntimeException", "Test"))
        .isInstanceOf(RuntimeException.class)
        .hasMessageContaining("Test");
  }

  @Test
  public void testGetArrayLength() {
    assertThat(getArrayLength(new int[] {1, 2, 3})).isEqualTo(3);
  }

  @Test
  public void testNewCharArray() {
    assertThat(newCharArray(3)).isEqualTo(new char[] {0, 0, 0});
  }

  @Test
  public void testNewShortArray() {
    assertThat(newShortArray(3)).isEqualTo(new short[] {0, 0, 0});
  }

  @Test
  public void testNewIntArray() {
    assertThat(newIntArray(3)).isEqualTo(new int[] {0, 0, 0});
  }

  @Test
  public void testNewLongArray() {
    assertThat(newLongArray(3)).isEqualTo(new long[] {0, 0, 0});
  }

  @Test
  public void testNewFloatArray() {
    assertThat(newFloatArray(3)).isEqualTo(new float[] {0, 0, 0});
  }

  @Test
  public void testNewDoubleArray() {
    assertThat(newDoubleArray(3)).isEqualTo(new double[] {0, 0, 0});
  }

  @Test
  public void testNewBooleanArray() {
    assertThat(newBooleanArray(3)).isEqualTo(new boolean[] {false, false, false});
  }

  @Test
  public void testNewObjectArray() {
    assertThat(newObjectArray(3, String.class, "foo"))
        .isEqualTo(new String[] {"foo", "foo", "foo"});
  }

  @Test
  public void testGetObjectArrayElement() {
    Object[] array = new Object[] {1, 2, 3};
    assertThat(getObjectArrayElement(array, 1)).isEqualTo(2);
  }

  @Test
  public void testSetObjectArrayElement() {
    Object[] array = new Object[] {1, 2, 3};
    setObjectArrayElement(array, 1, 200);
    assertThat(array).isEqualTo(new Object[] {1, 200, 3});
  }

  @Test
  public void testCallByteMethod() {
    assertThat(callByteMethod(JniHelperTest.class, this, "byteMethod", "()B")).isEqualTo((byte)1);
  }

  @Test
  public void testCallCharMethod() {
    assertThat(callCharMethod(JniHelperTest.class, this, "charMethod", "()C")).isEqualTo('a');
  }

  @Test
  public void testCallShortMethod() {
    assertThat(callShortMethod(JniHelperTest.class, this, "shortMethod", "()S"))
        .isEqualTo((short)1);
  }

  @Test
  public void testCallIntMethod() {
    assertThat(callIntMethod(JniHelperTest.class, this, "intMethod", "()I")).isEqualTo(1);
  }

  @Test
  public void testCallLongMethod() {
    assertThat(callLongMethod(JniHelperTest.class, this, "longMethod", "()J")).isEqualTo(1L);
  }

  @Test
  public void testCallFloatMethod() {
    assertThat(callFloatMethod(JniHelperTest.class, this, "floatMethod", "()F")).isEqualTo(3.14f);
  }

  @Test
  public void testCallDoubleMethod() {
    assertThat(callDoubleMethod(JniHelperTest.class, this, "doubleMethod", "()D")).isEqualTo(3.14);
  }

  @Test
  public void testCallBooleanMethod() {
    assertThat(callBooleanMethod(JniHelperTest.class, this, "booleanMethod", "()Z"))
        .isEqualTo(true);
  }

  @Test
  public void testCallVoidMethod() {
    callVoidMethod(JniHelperTest.class, this, "voidMethod", "()V");
  }

  @Test
  public void testCallObjectMethod() {
    assertThat(callObjectMethod(JniHelperTest.class, this, "objectMethod", "()Ljava/lang/String;"))
        .isEqualTo("Hello");
  }

  @Test
  public void testCallStaticByteMethod() {
    assertThat(callStaticByteMethod(JniHelperTest.class, "staticByteMethod", "()B"))
        .isEqualTo((byte)1);
  }

  @Test
  public void testCallStaticCharMethod() {
    assertThat(callStaticCharMethod(JniHelperTest.class, "staticCharMethod", "()C")).isEqualTo('a');
  }

  @Test
  public void testCallStaticShortMethod() {
    assertThat(callStaticShortMethod(JniHelperTest.class, "staticShortMethod", "()S"))
        .isEqualTo((short)1);
  }

  @Test
  public void testCallStaticIntMethod() {
    assertThat(callStaticIntMethod(JniHelperTest.class, "staticIntMethod", "()I")).isEqualTo(1);
  }

  @Test
  public void testCallStaticLongMethod() {
    assertThat(callStaticLongMethod(JniHelperTest.class, "staticLongMethod", "()J")).isEqualTo(1L);
  }

  @Test
  public void testCallStaticFloatMethod() {
    assertThat(callStaticFloatMethod(JniHelperTest.class, "staticFloatMethod", "()F"))
        .isEqualTo(3.14f);
  }

  @Test
  public void testCallStaticDoubleMethod() {
    assertThat(callStaticDoubleMethod(JniHelperTest.class, "staticDoubleMethod", "()D"))
        .isEqualTo(3.14);
  }

  @Test
  public void testCallStaticBooleanMethod() {
    assertThat(callStaticBooleanMethod(JniHelperTest.class, "staticBooleanMethod", "()Z"))
        .isEqualTo(true);
  }

  @Test
  public void testCallStaticVoidMethod() {
    callStaticVoidMethod(JniHelperTest.class, "staticVoidMethod", "()V");
  }

  @Test
  public void testCallStaticObjectMethod() {
    assertThat(
        callStaticObjectMethod(JniHelperTest.class, "staticObjectMethod", "()Ljava/lang/String;"))
        .isEqualTo("Hello");
  }
}
