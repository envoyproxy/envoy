#pragma once

#include <jni.h>

#include <memory>

#include "absl/strings/string_view.h"

namespace Envoy {
namespace JNI {

/** A custom deleter to delete JNI global ref. */
class GlobalRefDeleter {
public:
  explicit GlobalRefDeleter() = default;

  GlobalRefDeleter(const GlobalRefDeleter&) = default;

  // This is to allow move semantics in `GlobalRefUniquePtr`.
  GlobalRefDeleter& operator=(const GlobalRefDeleter&) = default;

  void operator()(jobject object) const;
};

/** A unique pointer for JNI global ref. */
template <typename T>
using GlobalRefUniquePtr = std::unique_ptr<typename std::remove_pointer<T>::type, GlobalRefDeleter>;

/** A custom deleter to delete JNI local ref. */
class LocalRefDeleter {
public:
  explicit LocalRefDeleter() = default;

  LocalRefDeleter(const LocalRefDeleter&) = default;

  // This is to allow move semantics in `LocalRefUniquePtr`.
  LocalRefDeleter& operator=(const LocalRefDeleter&) = default;

  void operator()(jobject object) const;
};

/** A unique pointer for JNI local ref. */
template <typename T>
using LocalRefUniquePtr = std::unique_ptr<typename std::remove_pointer<T>::type, LocalRefDeleter>;

/** A custom deleter for UTF strings. */
class StringUtfDeleter {
public:
  explicit StringUtfDeleter(jstring j_str) : j_str_(j_str) {}

  void operator()(const char* c_str) const;

private:
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
  explicit PrimitiveArrayCriticalDeleter(jarray array) : array_(array) {}

  void operator()(void* c_array) const;

private:
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

  struct Method {
    absl::string_view name_;
    absl::string_view signature_;
  };

  struct Field {
    absl::string_view name_;
    absl::string_view signature_;
  };

  /** Gets the JNI version supported. */
  static jint getVersion();

  /** Initializes the `JavaVM`. This function is typically called inside `JNI_OnLoad`. */
  static void initialize(JavaVM* java_vm);

  /** Performs a clean up. This function is typically called inside `JNI_OnUnload`. */
  static void finalize();

  /**
   * Adds the `jclass`, `jmethodID`, and `jfieldID` objects into a cache. This function is typically
   * called inside `JNI_OnLoad`.
   *
   * Caching the `jclass` can be useful for performance.
   * See https://developer.android.com/training/articles/perf-jni#jclass,-jmethodid,-and-jfieldid
   *
   * Another reason for caching the `jclass` object is to able to find a non-built-in class when the
   * native code creates a thread and then attaches it with `AttachCurrentThread`, i.e. calling
   * `getThreadLocalEnv()->getEnv()->FindClass`. This is because there are no stack frames from the
   * application. When calling `FindClass` from the thread, the `JavaVM` will start in the "system"
   * class loader instead of the one associated with the application, so attempts to find
   * app-specific classes will fail.
   *
   * See
   * https://developer.android.com/training/articles/perf-jni#faq:-why-didnt-findclass-find-my-class
   */
  static void addToCache(absl::string_view class_name, const std::vector<Method>& methods,
                         const std::vector<Method>& static_methods,
                         const std::vector<Field>& fields, const std::vector<Field>& static_fields);

  /** Gets the `JavaVM`. The `initialize(JavaVM*) must be called first. */
  static JavaVM* getJavaVm();

  /** Detaches the current thread from the `JavaVM`. */
  static void detachCurrentThread();

  /**
   * Gets the thread-local `JNIEnv`. This is useful for getting the `JNIEnv` between threads.
   * If the thread-local `JNIEnv` does not exist, this function will attach the current thread to
   * the JavaVM. Care must be taken to ensure `detachCurrentThread()` is called before the thread
   * exists to avoid a resource leak.
   *
   * See https://developer.android.com/training/articles/perf-jni#threads
   *
   * The `initialize(JavaVM*)` must be called first or else `JNIEnv` will return a `nullptr`.
   */
  static JNIEnv* getThreadLocalEnv();

  /** Gets the underlying `JNIEnv`. */
  JNIEnv* getEnv();

  /**
   * Gets the field ID for an instance field of a class.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getfieldid
   */
  jfieldID getFieldId(jclass clazz, const char* name, const char* signature);

  /**
   * Gets the field ID for an instance field of a class from the cache.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getfieldid
   */
  jfieldID getFieldIdFromCache(jclass clazz, const char* name, const char* signature);

  /**
   * Gets the field ID for a static field of a class.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getstaticfieldid
   */
  jfieldID getStaticFieldId(jclass clazz, const char* name, const char* signature);

  /**
   * Gets the field ID for a static field of a class from the cache.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getstaticfieldid
   */
  jfieldID getStaticFieldIdFromCache(jclass clazz, const char* name, const char* signature);

  /** A macro to create `Call<Type>Method` helper function. */
#define DECLARE_GET_FIELD(JAVA_TYPE, JNI_TYPE)                                                     \
  JNI_TYPE get##JAVA_TYPE##Field(jobject object, jfieldID field_id);

  /**
   * Helper functions for `Get<Type>Field`.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#gettypefield-routines
   */
  DECLARE_GET_FIELD(Byte, jbyte)
  DECLARE_GET_FIELD(Char, jchar)
  DECLARE_GET_FIELD(Short, jshort)
  DECLARE_GET_FIELD(Int, jint)
  DECLARE_GET_FIELD(Long, jlong)
  DECLARE_GET_FIELD(Float, jfloat)
  DECLARE_GET_FIELD(Double, jdouble)
  DECLARE_GET_FIELD(Boolean, jboolean)

  template <typename T = jobject>
  [[nodiscard]] LocalRefUniquePtr<T> getObjectField(jobject object, jfieldID field_id) {
    LocalRefUniquePtr<T> result(static_cast<T>(env_->GetObjectField(object, field_id)),
                                LocalRefDeleter());
    return result;
  }

  /**
   * Gets the object method with the given signature.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getmethodid
   */
  jmethodID getMethodId(jclass clazz, const char* name, const char* signature);

  /**
   * Gets the object method with the given signature from the cache.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getmethodid
   */
  jmethodID getMethodIdFromCache(jclass clazz, const char* name, const char* signature);

  /**
   * Gets the static method with the given signature.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getstaticmethodid
   */
  jmethodID getStaticMethodId(jclass clazz, const char* name, const char* signature);

  /**
   * Gets the static method with the given signature from the cache.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#getstaticmethodid
   */
  jmethodID getStaticMethodIdFromCache(jclass clazz, const char* name, const char* signature);

  /**
   * Finds the given `class_name` using from the cache.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#findclass
   */
  [[nodiscard]] jclass findClassFromCache(const char* class_name);

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
   * Returns true if an exception is being thrown; false otherwise.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#exceptionoccurred
   */
  [[nodiscard]] jboolean exceptionCheck();

  /**
   * Determines if an exception is being thrown. Returns a `nullptr` if there is no exception.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#exceptionoccurred
   */
  [[nodiscard]] LocalRefUniquePtr<jthrowable> exceptionOccurred();

  /**
   * Clears any exception that is currently being thrown. If no exception is currently being thrown,
   * this function has no effect.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#exceptionclear
   */
  void exceptionCleared();

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
                                LocalRefDeleter());
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
        PrimitiveArrayCriticalDeleter(array));
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
                                LocalRefDeleter());
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
        static_cast<T>(env_->CallStaticObjectMethodV(clazz, method_id, args)), LocalRefDeleter());
    va_end(args);
    rethrowException();
    return result;
  }

  /**
   * Allocates and returns a direct `java.nio.ByteBuffer` referring to the block of memory starting
   * at the memory address `address` and extending `capacity` bytes.
   *
   * https://docs.oracle.com/en/java/javase/17/docs/specs/jni/functions.html#newdirectbytebuffer
   */
  LocalRefUniquePtr<jobject> newDirectByteBuffer(void* address, jlong capacity);

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
