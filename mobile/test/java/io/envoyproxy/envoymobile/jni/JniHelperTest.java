package io.envoyproxy.envoymobile.jni;

import static com.google.common.truth.Truth.assertThat;
import static org.junit.Assert.assertThrows;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

@RunWith(RobolectricTestRunner.class)
public class JniHelperTest {
  public JniHelperTest() { System.loadLibrary("envoy_jni_helper_test"); }

  //================================================================================
  // Native methods for testing.
  //================================================================================
  public static native void getFieldId(Class<?> clazz, String name, String signature);
  public static native void getFieldIdFromCache(String className, String fieldName,
                                                String signature);
  public static native void getStaticFieldId(Class<?> clazz, String name, String signature);
  public static native void getStaticFieldIdFromCache(String className, String fieldName,
                                                      String signature);
  public static native byte getByteField(Class<?> clazz, Object instance, String name,
                                         String signature);
  public static native char getCharField(Class<?> clazz, Object instance, String name,
                                         String signature);
  public static native short getShortField(Class<?> clazz, Object instance, String name,
                                           String signature);
  public static native int getIntField(Class<?> clazz, Object instance, String name,
                                       String signature);
  public static native long getLongField(Class<?> clazz, Object instance, String name,
                                         String signature);
  public static native float getFloatField(Class<?> clazz, Object instance, String name,
                                           String signature);
  public static native double getDoubleField(Class<?> clazz, Object instance, String name,
                                             String signature);
  public static native boolean getBooleanField(Class<?> clazz, Object instance, String name,
                                               String signature);
  public static native Object getObjectField(Class<?> clazz, Object instance, String name,
                                             String signature);
  public static native void getMethodId(Class<?> clazz, String name, String signature);
  public static native void getMethodIdFromCache(String className, String methodName,
                                                 String signature);
  public static native void getStaticMethodId(Class<?> clazz, String name, String signature);
  public static native void getStaticMethodIdFromCache(String className, String methodName,
                                                       String signature);
  public static native Class<?> findClassFromCache(String className);
  public static native Class<?> getObjectClass(Object object);
  public static native Object newObject(Class<?> clazz, String name, String signature);
  public static native void throwNew(String className, String message);
  public static native boolean exceptionOccurred(Class<?> clazz, String name, String signature);
  public static native boolean exceptionClear(Class<?> clazz, String name, String signature);
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
  public static native byte[] getByteArrayElements(byte[] array);
  public static native char[] getCharArrayElements(char[] array);
  public static native short[] getShortArrayElements(short[] array);
  public static native int[] getIntArrayElements(int[] array);
  public static native long[] getLongArrayElements(long[] array);
  public static native float[] getFloatArrayElements(float[] array);
  public static native double[] getDoubleArrayElements(double[] array);
  public static native boolean[] getBooleanArrayElements(boolean[] array);
  public static native Object getObjectArrayElement(Object[] array, int index);
  public static native void setObjectArrayElement(Object[] array, int index, Object value);
  public static native void setByteArrayRegion(byte[] array, int start, int index, byte[] buffer);
  public static native void setCharArrayRegion(char[] array, int start, int index, char[] buffer);
  public static native void setShortArrayRegion(short[] array, int start, int index,
                                                short[] buffer);
  public static native void setIntArrayRegion(int[] array, int start, int index, int[] buffer);
  public static native void setLongArrayRegion(long[] array, int start, int index, long[] buffer);
  public static native void setFloatArrayRegion(float[] array, int start, int index,
                                                float[] buffer);
  public static native void setDoubleArrayRegion(double[] array, int start, int index,
                                                 double[] buffer);
  public static native void setBooleanArrayRegion(boolean[] array, int start, int index,
                                                  boolean[] buffer);
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
  public static native Object newDirectByteBuffer();

  //================================================================================
  // Fields used for Get<Type>Field tests.
  //================================================================================
  private final byte byteField = 1;
  private final char charField = 'a';
  private final short shortField = 1;
  private final int intField = 1;
  private final long longField = 1;
  private final float floatField = 3.14f;
  private final double doubleField = 3.14;
  private final boolean booleanField = true;
  private final String objectField = "Hello";

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

  //================================================================================
  // Methods used for Exception* tests.
  //================================================================================
  public static void alwaysThrow() { throw new RuntimeException("Test"); }

  static class Foo {
    private final int field = 1;
    private static int staticField = 2;
    private static void staticMethod() {}
  }

  @Test
  public void testGetFieldId() {
    getFieldId(Foo.class, "field", "I");
  }

  @Test
  public void testGetFieldIdFromCache() {
    // Do it in a loop to test the cache.
    for (int i = 0; i < 10; i++) {
      getFieldIdFromCache("io/envoyproxy/envoymobile/jni/JniHelperTest$Foo", "field", "I");
    }
  }

  @Test
  public void testGetStaticFieldId() {
    getStaticFieldId(Foo.class, "staticField", "I");
  }

  @Test
  public void testGetStaticFieldIdFromCache() {
    // Do it in a loop to test the cache.
    for (int i = 0; i < 10; i++) {
      getStaticFieldIdFromCache("io/envoyproxy/envoymobile/jni/JniHelperTest$Foo", "staticField",
                                "I");
    }
  }

  @Test
  public void testGetByteField() {
    assertThat(getByteField(JniHelperTest.class, this, "byteField", "B")).isEqualTo(1);
  }

  @Test
  public void testGetCharField() {
    assertThat(getCharField(JniHelperTest.class, this, "charField", "C")).isEqualTo('a');
  }

  @Test
  public void testGetShortField() {
    assertThat(getShortField(JniHelperTest.class, this, "shortField", "S")).isEqualTo(1);
  }

  @Test
  public void testGetIntField() {
    assertThat(getIntField(JniHelperTest.class, this, "intField", "I")).isEqualTo(1);
  }

  @Test
  public void testGetLongField() {
    assertThat(getLongField(JniHelperTest.class, this, "longField", "J")).isEqualTo(1L);
  }

  @Test
  public void testGetFloatField() {
    assertThat(getFloatField(JniHelperTest.class, this, "floatField", "F")).isEqualTo(3.14f);
  }

  @Test
  public void testGetDoubleField() {
    assertThat(getDoubleField(JniHelperTest.class, this, "doubleField", "D")).isEqualTo(3.14);
  }

  @Test
  public void testGetBooleanField() {
    assertThat(getBooleanField(JniHelperTest.class, this, "booleanField", "Z")).isEqualTo(true);
  }

  @Test
  public void testGetObjectField() {
    assertThat(getObjectField(JniHelperTest.class, this, "objectField", "Ljava/lang/String;"))
        .isEqualTo("Hello");
  }

  @Test
  public void testGetMethodId() {
    getMethodId(Foo.class, "<init>", "()V");
  }

  @Test
  public void testGetMethodIdFromCache() {
    // Do it in a loop to test the cache.
    for (int i = 0; i < 10; i++) {
      getMethodIdFromCache("io/envoyproxy/envoymobile/jni/JniHelperTest$Foo", "<init>", "()V");
    }
  }

  @Test
  public void testGetStaticMethodId() {
    getStaticMethodId(Foo.class, "staticMethod", "()V");
  }

  @Test
  public void testGetStaticMethodIdFromCache() {
    // Do it in a loop to test the cache.
    for (int i = 0; i < 10; i++) {
      getStaticMethodIdFromCache("io/envoyproxy/envoymobile/jni/JniHelperTest$Foo", "staticMethod",
                                 "()V");
    }
  }

  @Test
  public void testFindClassFromCache() {
    // Do it in a loop to test the cache.
    for (int i = 0; i < 10; i++) {
      assertThat(findClassFromCache("java/lang/Exception")).isEqualTo(Exception.class);
    }
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
    RuntimeException exception =
        assertThrows(RuntimeException.class, () -> throwNew("java/lang/RuntimeException", "Test"));
    assertThat(exception).hasMessageThat().contains("Test");
  }

  @Test
  public void testExceptionOccurred() {
    RuntimeException exception = assertThrows(
        RuntimeException.class, () -> exceptionOccurred(JniHelperTest.class, "alwaysThrow", "()V"));
    assertThat(exception).hasMessageThat().contains("Test");
  }

  @Test
  public void testExceptionClear() {
    assertThat(exceptionClear(JniHelperTest.class, "alwaysThrow", "()V")).isTrue();
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
  public void testGetByteArrayElements() {
    assertThat(getByteArrayElements(new byte[] {0, 0, 0})).isEqualTo(new byte[] {123, 123, 123});
  }

  @Test
  public void testGetCharArrayElements() {
    assertThat(getCharArrayElements(new char[] {' ', ' ', ' '}))
        .isEqualTo(new char[] {'a', 'a', 'a'});
  }

  @Test
  public void testGetShortArrayElements() {
    assertThat(getShortArrayElements(new short[] {0, 0, 0})).isEqualTo(new short[] {123, 123, 123});
  }

  @Test
  public void testGetIntArrayElements() {
    assertThat(getIntArrayElements(new int[] {0, 0, 0})).isEqualTo(new int[] {123, 123, 123});
  }

  @Test
  public void testGetLongArrayElements() {
    assertThat(getLongArrayElements(new long[] {0, 0, 0})).isEqualTo(new long[] {123, 123, 123});
  }

  @Test
  public void testGetFloatArrayElements() {
    assertThat(getFloatArrayElements(new float[] {0, 0, 0}))
        .isEqualTo(new float[] {3.14f, 3.14f, 3.14f});
  }

  @Test
  public void testGetDoubleArrayElements() {
    assertThat(getDoubleArrayElements(new double[] {0, 0, 0}))
        .isEqualTo(new double[] {3.14, 3.14, 3.14});
  }

  @Test
  public void testGetBooleanArrayElements() {
    assertThat(getBooleanArrayElements(new boolean[] {false, false, false}))
        .isEqualTo(new boolean[] {true, true, true});
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
  public void testSetByteArrayRegion() {
    byte[] array = new byte[] {1, 0, 0, 0, 5};
    byte[] buffer = new byte[] {2, 3, 4};
    setByteArrayRegion(array, 1, 3, buffer);
    assertThat(array).isEqualTo(new byte[] {1, 2, 3, 4, 5});
  }

  @Test
  public void testSetCharArrayRegion() {
    char[] array = new char[] {'a', ' ', ' ', ' ', 'e'};
    char[] buffer = new char[] {'b', 'c', 'd'};
    setCharArrayRegion(array, 1, 3, buffer);
    assertThat(array).isEqualTo(new char[] {'a', 'b', 'c', 'd', 'e'});
  }

  @Test
  public void testSetShortArrayRegion() {
    short[] array = new short[] {1, 0, 0, 0, 5};
    short[] buffer = new short[] {2, 3, 4};
    setShortArrayRegion(array, 1, 3, buffer);
    assertThat(array).isEqualTo(new short[] {1, 2, 3, 4, 5});
  }

  @Test
  public void testSetIntArrayRegion() {
    int[] array = new int[] {1, 0, 0, 0, 5};
    int[] buffer = new int[] {2, 3, 4};
    setIntArrayRegion(array, 1, 3, buffer);
    assertThat(array).isEqualTo(new int[] {1, 2, 3, 4, 5});
  }

  @Test
  public void testSetLongArrayRegion() {
    long[] array = new long[] {1, 0, 0, 0, 5};
    long[] buffer = new long[] {2, 3, 4};
    setLongArrayRegion(array, 1, 3, buffer);
    assertThat(array).isEqualTo(new long[] {1, 2, 3, 4, 5});
  }

  @Test
  public void testSetFloatArrayRegion() {
    float[] array = new float[] {1, 0, 0, 0, 5};
    float[] buffer = new float[] {2, 3, 4};
    setFloatArrayRegion(array, 1, 3, buffer);
    assertThat(array).isEqualTo(new float[] {1, 2, 3, 4, 5});
  }

  @Test
  public void testSetDoubleArrayRegion() {
    double[] array = new double[] {1, 0, 0, 0, 5};
    double[] buffer = new double[] {2, 3, 4};
    setDoubleArrayRegion(array, 1, 3, buffer);
    assertThat(array).isEqualTo(new double[] {1, 2, 3, 4, 5});
  }

  @Test
  public void testSetBooleanArrayRegion() {
    boolean[] array = new boolean[] {true, false, false, false, true};
    boolean[] buffer = new boolean[] {true, true, true};
    setBooleanArrayRegion(array, 1, 3, buffer);
    assertThat(array).isEqualTo(new boolean[] {true, true, true, true, true});
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

  @Test
  public void testNewDirectByteBuffer() {
    ByteBuffer byteBuffer = ((ByteBuffer)newDirectByteBuffer()).order(ByteOrder.LITTLE_ENDIAN);
    assertThat(byteBuffer.capacity()).isEqualTo(3);
    assertThat(byteBuffer.get(0)).isEqualTo(1);
    assertThat(byteBuffer.get(1)).isEqualTo(2);
    assertThat(byteBuffer.get(2)).isEqualTo(3);
  }
}
