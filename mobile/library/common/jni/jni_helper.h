#pragma once

#include <memory>

#include "library/common/jni/import/jni_import.h"

namespace Envoy {
namespace JNI {

/** A custom deleter to delete JNI global ref. */
class GlobalRefDeleter {
public:
  explicit GlobalRefDeleter(JNIEnv* env) : env_(env) {}

  void operator()(jobject object) const {
    if (object != nullptr) {
      env_->DeleteGlobalRef(object);
    }
  }

private:
  JNIEnv* const env_;
};

/** A unique pointer for JNI global ref. */
template <typename T>
using GlobalRefUniquePtr = std::unique_ptr<typename std::remove_pointer<T>::type, GlobalRefDeleter>;

/** A custom deleter to delete JNI local ref. */
class LocalRefDeleter {
public:
  explicit LocalRefDeleter(JNIEnv* env) : env_(env) {}

  // This is to allow move semantics in `LocalRefUniquePtr`.
  LocalRefDeleter& operator=(const LocalRefDeleter&) { return *this; }

  void operator()(jobject object) const {
    if (object != nullptr) {
      env_->DeleteLocalRef(object);
    }
  }

private:
  JNIEnv* const env_;
};

/** A unique pointer for JNI local ref. */
template <typename T>
using LocalRefUniquePtr = std::unique_ptr<typename std::remove_pointer<T>::type, LocalRefDeleter>;

/** A custom deleter for UTF strings. */
class StringUtfDeleter {
public:
  StringUtfDeleter(JNIEnv* env, jstring j_str) : env_(env), j_str_(j_str) {}

  void operator()(const char* c_str) const {
    if (c_str != nullptr) {
      env_->ReleaseStringUTFChars(j_str_, c_str);
    }
  }

private:
  JNIEnv* const env_;
  jstring j_str_;
};

/** A unique pointer for JNI UTF string. */
using StringUtfUniquePtr = std::unique_ptr<const char, StringUtfDeleter>;

/** A custom deleter to delete JNI array elements. */
template <typename ArrayType, typename ElementType> class ArrayElementsDeleter {
public:
  ArrayElementsDeleter(JNIEnv* env, ArrayType array) : env_(env), array_(array) {}

  void operator()(ElementType* elements) const {
    if (elements == nullptr) {
      return;
    }
    if constexpr (std::is_same_v<ElementType, jbyte>) {
      env_->ReleaseByteArrayElements(array_, elements, 0);
    } else if constexpr (std::is_same_v<ElementType, jchar>) {
      env_->ReleaseCharArrayElements(array_, elements, 0);
    } else if constexpr (std::is_same_v<ElementType, jshort>) {
      env_->ReleaseShortArrayElements(array_, elements, 0);
    } else if constexpr (std::is_same_v<ElementType, jint>) {
      env_->ReleaseIntArrayElements(array_, elements, 0);
    } else if constexpr (std::is_same_v<ElementType, jlong>) {
      env_->ReleaseLongArrayElements(array_, elements, 0);
    } else if constexpr (std::is_same_v<ElementType, jfloat>) {
      env_->ReleaseFloatArrayElements(array_, elements, 0);
    } else if constexpr (std::is_same_v<ElementType, jdouble>) {
      env_->ReleaseDoubleArrayElements(array_, elements, 0);
    } else if constexpr (std::is_same_v<ElementType, jboolean>) {
      env_->ReleaseBooleanArrayElements(array_, elements, 0);
    }
  }

private:
  JNIEnv* const env_;
  ArrayType array_;
};

/** A unique pointer for JNI array elements. */
template <typename ArrayType, typename ElementType>
using ArrayElementsUniquePtr = std::unique_ptr<
    typename std::remove_pointer<ElementType>::type,
    ArrayElementsDeleter<ArrayType, typename std::remove_pointer<ElementType>::type>>;

/** A custom deleter for JNI primitive array critical. */
class PrimitiveArrayCriticalDeleter {
public:
  PrimitiveArrayCriticalDeleter(JNIEnv* env, jarray array) : env_(env), array_(array) {}

  void operator()(void* c_array) const {
    if (c_array != nullptr) {
      env_->ReleasePrimitiveArrayCritical(array_, c_array, 0);
    }
  }

private:
  JNIEnv* const env_;
  jarray array_;
};

/** A unique pointer for JNI primitive array critical. */
template <typename T>
using PrimitiveArrayCriticalUniquePtr =
    std::unique_ptr<typename std::remove_pointer<T>::type, PrimitiveArrayCriticalDeleter>;

/**
 * A thin wrapper around JNI API with automatic memory management.
 *
 * NOTE: Do not put any other helper functions that are not part of the JNI API here.
 */
class JniHelper {
public:
  explicit JniHelper(JNIEnv* env) : env_(env) {}

  /** Gets the underlying `JNIEnv`. */
  JNIEnv* getEnv();

  /**
   * Gets the object method with the given signature.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getmethodid
   */
  jmethodID getMethodId(jclass clazz, const char* name, const char* signature);

  /**
   * Gets the static method with the given signature.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getstaticmethodid
   */
  jmethodID getStaticMethodId(jclass clazz, const char* name, const char* signature);

  /**
   * Finds the given `class_name` using Java classloader.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#findclass
   */
  [[nodiscard]] LocalRefUniquePtr<jclass> findClass(const char* class_name);

  /**
   * Returns the class of a given `object`.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getobjectclass
   */
  [[nodiscard]] LocalRefUniquePtr<jclass> getObjectClass(jobject object);

  /**
   * Throws Java exception with the specified class name and error message.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#thrownew
   */
  void throwNew(const char* java_class_name, const char* message);

  /**
   * Determines if an exception is being thrown.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#exceptionoccurred
   */
  [[nodiscard]] LocalRefUniquePtr<jthrowable> exceptionOccurred();

  /**
   * Creates a new global reference to the object referred to by the `object` argument.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#newglobalref
   */
  [[nodiscard]] GlobalRefUniquePtr<jobject> newGlobalRef(jobject object);

  /**
   * Creates a new instance of a given `clazz` from the given `method_id`.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#newobject-newobjecta-newobjectv
   */
  [[nodiscard]] LocalRefUniquePtr<jobject> newObject(jclass clazz, jmethodID method_id, ...);

  /**
   * Creates a new Java string from the given `str`.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#newstringutf
   */
  [[nodiscard]] LocalRefUniquePtr<jstring> newStringUtf(const char* str);

  /** Gets the pointer to an array of bytes representing `str`. */
  [[nodiscard]] StringUtfUniquePtr getStringUtfChars(jstring str, jboolean* is_copy);

  /**
   * Gets the size of the array.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getarraylength
   */
  jsize getArrayLength(jarray array);

/** A macro to create `New<Type>Array`. helper function. */
#define DECLARE_NEW_ARRAY(JAVA_TYPE, JNI_TYPE)                                                     \
  [[nodiscard]] LocalRefUniquePtr<JNI_TYPE> new##JAVA_TYPE##Array(jsize length);

  /**
   * Helper functions for `New<Type>Array`.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#newprimitivetypearray-routines
   */
  DECLARE_NEW_ARRAY(Byte, jbyteArray)
  DECLARE_NEW_ARRAY(Char, jcharArray)
  DECLARE_NEW_ARRAY(Short, jshortArray)
  DECLARE_NEW_ARRAY(Int, jintArray)
  DECLARE_NEW_ARRAY(Long, jlongArray)
  DECLARE_NEW_ARRAY(Float, jfloatArray)
  DECLARE_NEW_ARRAY(Double, jdoubleArray)
  DECLARE_NEW_ARRAY(Boolean, jbooleanArray)
  [[nodiscard]] LocalRefUniquePtr<jobjectArray> newObjectArray(jsize length, jclass element_class,
                                                               jobject initial_element = nullptr);

/** A macro to create `Get<JavaType>ArrayElement` function. */
#define DECLARE_GET_ARRAY_ELEMENTS(JAVA_TYPE, JNI_ARRAY_TYPE, JNI_ELEMENT_TYPE)                    \
  [[nodiscard]] ArrayElementsUniquePtr<JNI_ARRAY_TYPE, JNI_ELEMENT_TYPE>                           \
      get##JAVA_TYPE##ArrayElements(JNI_ARRAY_TYPE array, jboolean* is_copy);

  /**
   * Helper functions for `Get<JavaType>ArrayElements`.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getprimitivetypearrayelements-routines
   */
  DECLARE_GET_ARRAY_ELEMENTS(Byte, jbyteArray, jbyte)
  DECLARE_GET_ARRAY_ELEMENTS(Char, jcharArray, jchar)
  DECLARE_GET_ARRAY_ELEMENTS(Short, jshortArray, jshort)
  DECLARE_GET_ARRAY_ELEMENTS(Int, jintArray, jint)
  DECLARE_GET_ARRAY_ELEMENTS(Long, jlongArray, jlong)
  DECLARE_GET_ARRAY_ELEMENTS(Float, jfloatArray, jfloat)
  DECLARE_GET_ARRAY_ELEMENTS(Double, jdoubleArray, jdouble)
  DECLARE_GET_ARRAY_ELEMENTS(Boolean, jbooleanArray, jboolean)

  /**
   * Gets an element of a given `array` with the specified `index`.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getobjectarrayelement
   */
  template <typename T = jobject>
  [[nodiscard]] LocalRefUniquePtr<T> getObjectArrayElement(jobjectArray array, jsize index) {
    LocalRefUniquePtr<T> result(static_cast<T>(env_->GetObjectArrayElement(array, index)),
                                LocalRefDeleter(env_));
    rethrowException();
    return result;
  }

  /**
   * Sets an element of a given `array` with the specified `index.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#setobjectarrayelement
   */
  void setObjectArrayElement(jobjectArray array, jsize index, jobject value);

  /**
   * Returns the pointer into the primitive array.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getprimitivearraycritical-releaseprimitivearraycritical
   */
  template <typename T = void*>
  [[nodiscard]] PrimitiveArrayCriticalUniquePtr<T> getPrimitiveArrayCritical(jarray array,
                                                                             jboolean* is_copy) {
    PrimitiveArrayCriticalUniquePtr<T> result(
        static_cast<T>(env_->GetPrimitiveArrayCritical(array, is_copy)),
        PrimitiveArrayCriticalDeleter(env_, array));
    return result;
  }

  /**
   * Sets a region of an `array` from a `buffer` with the specified `start` index and `length`.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#setprimitivetypearrayregion-routines
   */
#define DECLARE_SET_ARRAY_REGION(JAVA_TYPE, JNI_ARRAY_TYPE, JNI_ELEMENT_TYPE)                      \
  void set##JAVA_TYPE##ArrayRegion(JNI_ARRAY_TYPE array, jsize start, jsize length,                \
                                   const JNI_ELEMENT_TYPE* buffer);

  DECLARE_SET_ARRAY_REGION(Byte, jbyteArray, jbyte)
  DECLARE_SET_ARRAY_REGION(Char, jcharArray, jchar)
  DECLARE_SET_ARRAY_REGION(Short, jshortArray, jshort)
  DECLARE_SET_ARRAY_REGION(Int, jintArray, jint)
  DECLARE_SET_ARRAY_REGION(Long, jlongArray, jlong)
  DECLARE_SET_ARRAY_REGION(Float, jfloatArray, jfloat)
  DECLARE_SET_ARRAY_REGION(Double, jdoubleArray, jdouble)
  DECLARE_SET_ARRAY_REGION(Boolean, jbooleanArray, jboolean)

/** A macro to create `Call<Type>Method` helper function. */
#define DECLARE_CALL_METHOD(JAVA_TYPE, JNI_TYPE)                                                   \
  JNI_TYPE call##JAVA_TYPE##Method(jobject object, jmethodID method_id, ...);

  /**
   * Helper functions for `Call<Type>Method`.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#calltypemethod-routines-calltypemethoda-routines-calltypemethodv-routines
   */
  DECLARE_CALL_METHOD(Byte, jbyte)
  DECLARE_CALL_METHOD(Char, jchar)
  DECLARE_CALL_METHOD(Short, jshort)
  DECLARE_CALL_METHOD(Int, jint)
  DECLARE_CALL_METHOD(Long, jlong)
  DECLARE_CALL_METHOD(Float, jfloat)
  DECLARE_CALL_METHOD(Double, jdouble)
  DECLARE_CALL_METHOD(Boolean, jboolean)

  void callVoidMethod(jobject object, jmethodID method_id, ...);

  template <typename T = jobject>
  [[nodiscard]] LocalRefUniquePtr<T> callObjectMethod(jobject object, jmethodID method_id, ...) {
    va_list args;
    va_start(args, method_id);
    LocalRefUniquePtr<T> result(static_cast<T>(env_->CallObjectMethodV(object, method_id, args)),
                                LocalRefDeleter(env_));
    va_end(args);
    rethrowException();
    return result;
  }

/** A macro to create `CallStatic<Type>Method` helper function. */
#define DECLARE_CALL_STATIC_METHOD(JAVA_TYPE, JNI_TYPE)                                            \
  JNI_TYPE callStatic##JAVA_TYPE##Method(jclass clazz, jmethodID method_id, ...);

  /**
   * Helper functions for `CallStatic<Type>Method`.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#callstatictypemethod-routines-callstatictypemethoda-routines-callstatictypemethodv-routines
   */
  DECLARE_CALL_STATIC_METHOD(Byte, jbyte)
  DECLARE_CALL_STATIC_METHOD(Char, jchar)
  DECLARE_CALL_STATIC_METHOD(Short, jshort)
  DECLARE_CALL_STATIC_METHOD(Int, jint)
  DECLARE_CALL_STATIC_METHOD(Long, jlong)
  DECLARE_CALL_STATIC_METHOD(Float, jfloat)
  DECLARE_CALL_STATIC_METHOD(Double, jdouble)
  DECLARE_CALL_STATIC_METHOD(Boolean, jboolean)

  void callStaticVoidMethod(jclass clazz, jmethodID method_id, ...);

  template <typename T = jobject>
  [[nodiscard]] LocalRefUniquePtr<T> callStaticObjectMethod(jclass clazz, jmethodID method_id,
                                                            ...) {
    va_list args;
    va_start(args, method_id);
    LocalRefUniquePtr<T> result(
        static_cast<T>(env_->CallStaticObjectMethodV(clazz, method_id, args)),
        LocalRefDeleter(env_));
    va_end(args);
    rethrowException();
    return result;
  }

  /**
   * Returns the capacity of the memory region referenced by the given `java.nio.Buffer` object.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getdirectbuffercapacity
   */
  jlong getDirectBufferCapacity(jobject buffer);

  /**
   * Gets the address of memory associated with the given NIO direct buffer.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getdirectbufferaddress
   */
  template <typename T = void*> T getDirectBufferAddress(jobject buffer) {
    return static_cast<T>(env_->GetDirectBufferAddress(buffer));
  }

private:
  /** Rethrows the Java exception occurred. */
  void rethrowException();

  JNIEnv* const env_;
};

} // namespace JNI
} // namespace Envoy
