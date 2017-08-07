// Copyright 2017 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef STARBOARD_ANDROID_SHARED_JNI_ENV_EXT_H_
#define STARBOARD_ANDROID_SHARED_JNI_ENV_EXT_H_

#include <android/native_activity.h>
#include <jni.h>

#include <cstdarg>

#include "starboard/log.h"

namespace starboard {
namespace android {
namespace shared {

// An extension to JNIEnv to simplify making JNI calls.
//
// Call the static Get() method to get an instance that is already attached to
// the JVM in the current thread.
//
// This extends the JNIEnv structure, which already has a C++ interface for
// calling JNI methods, so any JNIEnv method can be called directly on this.
//
// There are convenience methods to lookup and call Java methods on object
// instances in a single step, with even simpler methods to call Java methods on
// the Activity (the CobaltActivity).
struct JniEnvExt : public JNIEnv {
  // One-time initialization to be called before starting the application.
  static void Initialize(ANativeActivity* native_activity);

  // Called right before each native thread is about to be shutdown.
  static void OnThreadShutdown();

  // Returns the thread-specific instance of JniEnvExt.
  static JniEnvExt* Get();

  // Returns the CobaltActivity object.
  jobject GetActivityObject();

  // Lookup the class of an object and find a method in it.
  jmethodID GetObjectMethodIDOrAbort(jobject obj,
                                     const char* name,
                                     const char* sig) {
    jclass clazz = GetObjectClass(obj);
    AbortOnException();
    jmethodID method_id = GetMethodID(clazz, name, sig);
    AbortOnException();
    DeleteLocalRef(clazz);
    return method_id;
  }

  jmethodID GetStaticMethodIDOrAbort(jclass clazz,
                                     const char* name,
                                     const char* sig) {
    jmethodID method = GetStaticMethodID(clazz, name, sig);
    AbortOnException();
    return method;
  }

  jobject GetObjectArrayElementOrAbort(jobjectArray array, jsize index) {
    jobject result = GetObjectArrayElement(array, index);
    AbortOnException();
    return result;
  }

  // Find a class by name using the Activity's class loader. This can load
  // both system classes and application classes, even when not in a JNI
  // stack frame (e.g. in a native thread that was attached the the JVM).
  // https://developer.android.com/training/articles/perf-jni.html#faq_FindClass
  jclass FindClassExtOrAbort(const char* name);

  jclass FindClassOrAbort(const char* name) {
    jclass result = FindClass(name);
    AbortOnException();
    return result;
  }

  // Convienience method to lookup and call a constructor.
  jobject NewObjectOrAbort(const char* class_name, const char* sig, ...) {
    va_list argp;
    va_start(argp, sig);
    jclass clazz = FindClassExtOrAbort(class_name);
    jmethodID methodID = GetMethodID(clazz, "<init>", sig);
    AbortOnException();
    jobject result = NewObjectV(clazz, methodID, argp);
    AbortOnException();
    DeleteLocalRef(clazz);
    va_end(argp);
    return result;
  }

  jstring NewStringUTFOrAbort(const char* bytes) {
    jstring result = NewStringUTF(bytes);
    AbortOnException();
    return result;
  }

// Convienience methods to lookup and call a method all at once:
// Call[Type]Method() overloaded to take a jobject of an instance.
// CallActivity[Type]Method() to call methods on the CobaltActivity.
#define X(_jtype, _jname)                                                      \
  _jtype Call##_jname##Method(jobject obj, const char* name, const char* sig,  \
                              ...) {                                           \
    va_list argp;                                                              \
    va_start(argp, sig);                                                       \
    _jtype result = Call##_jname##MethodV(                                     \
        obj, GetObjectMethodIDOrAbort(obj, name, sig), argp);                  \
    va_end(argp);                                                              \
    return result;                                                             \
  }                                                                            \
                                                                               \
  _jtype Call##_jname##MethodOrAbort(jobject obj, const char* name,            \
                                     const char* sig, ...) {                   \
    va_list argp;                                                              \
    va_start(argp, sig);                                                       \
    _jtype result = Call##_jname##MethodVOrAbort(                              \
        obj, GetObjectMethodIDOrAbort(obj, name, sig), argp);                  \
    va_end(argp);                                                              \
    return result;                                                             \
  }                                                                            \
                                                                               \
  _jtype CallActivity##_jname##Method(const char* name, const char* sig,       \
                                      ...) {                                   \
    va_list argp;                                                              \
    va_start(argp, sig);                                                       \
    jobject obj = GetActivityObject();                                         \
    _jtype result = Call##_jname##MethodV(                                     \
        obj, GetObjectMethodIDOrAbort(obj, name, sig), argp);                  \
    va_end(argp);                                                              \
    return result;                                                             \
  }                                                                            \
                                                                               \
  _jtype CallActivity##_jname##MethodOrAbort(const char* name,                 \
                                             const char* sig, ...) {           \
    va_list argp;                                                              \
    va_start(argp, sig);                                                       \
    jobject obj = GetActivityObject();                                         \
    _jtype result = Call##_jname##MethodVOrAbort(                              \
        obj, GetObjectMethodIDOrAbort(obj, name, sig), argp);                  \
    va_end(argp);                                                              \
    return result;                                                             \
  }                                                                            \
                                                                               \
  _jtype CallStatic##_jname##Method(                                           \
      const char* class_name, const char* method_name, const char* sig, ...) { \
    va_list argp;                                                              \
    va_start(argp, sig);                                                       \
    jclass clazz = FindClassExtOrAbort(class_name);                            \
    _jtype result = CallStatic##_jname##MethodV(                               \
        clazz, GetStaticMethodIDOrAbort(clazz, method_name, sig), argp);       \
    DeleteLocalRef(clazz);                                                     \
    va_end(argp);                                                              \
    return result;                                                             \
  }                                                                            \
                                                                               \
  _jtype CallStatic##_jname##MethodOrAbort(                                    \
      const char* class_name, const char* method_name, const char* sig, ...) { \
    va_list argp;                                                              \
    va_start(argp, sig);                                                       \
    jclass clazz = FindClassExtOrAbort(class_name);                            \
    _jtype result = CallStatic##_jname##MethodVOrAbort(                        \
        clazz, GetStaticMethodIDOrAbort(clazz, method_name, sig), argp);       \
    DeleteLocalRef(clazz);                                                     \
    va_end(argp);                                                              \
    return result;                                                             \
  }                                                                            \
                                                                               \
  _jtype Call##_jname##MethodVOrAbort(jobject obj, jmethodID methodID,         \
                                      va_list args) {                          \
    _jtype result = Call##_jname##MethodV(obj, methodID, args);                \
    AbortOnException();                                                        \
    return result;                                                             \
  }                                                                            \
                                                                               \
  _jtype CallStatic##_jname##MethodVOrAbort(jclass clazz, jmethodID methodID,  \
                                            va_list args) {                    \
    _jtype result = CallStatic##_jname##MethodV(clazz, methodID, args);        \
    AbortOnException();                                                        \
    return result;                                                             \
  }

  X(jobject, Object)
  X(jboolean, Boolean)
  X(jbyte, Byte)
  X(jchar, Char)
  X(jshort, Short)
  X(jint, Int)
  X(jlong, Long)
  X(jfloat, Float)
  X(jdouble, Double)

#undef X

  void CallVoidMethod(jobject obj, const char* name, const char* sig, ...) {
    va_list argp;
    va_start(argp, sig);
    CallVoidMethodV(obj, GetObjectMethodIDOrAbort(obj, name, sig), argp);
    va_end(argp);
  }

  void CallVoidMethodOrAbort(jobject obj,
                             const char* name,
                             const char* sig,
                             ...) {
    va_list argp;
    va_start(argp, sig);
    CallVoidMethodVOrAbort(obj, GetObjectMethodIDOrAbort(obj, name, sig), argp);
    va_end(argp);
  }

  void CallVoidMethodVOrAbort(jobject obj, jmethodID methodID, va_list args) {
    CallVoidMethodV(obj, methodID, args);
    AbortOnException();
  }

  void CallActivityVoidMethod(const char* name, const char* sig, ...) {
    va_list argp;
    va_start(argp, sig);
    jobject obj = GetActivityObject();
    CallVoidMethodV(obj, GetObjectMethodIDOrAbort(obj, name, sig), argp);
    va_end(argp);
  }

  void CallActivityVoidMethodOrAbort(const char* name, const char* sig, ...) {
    va_list argp;
    va_start(argp, sig);
    jobject obj = GetActivityObject();
    CallVoidMethodVOrAbort(obj, GetObjectMethodIDOrAbort(obj, name, sig), argp);
    va_end(argp);
  }

  void CallStaticVoidMethod(const char* class_name,
                            const char* method_name,
                            const char* sig,
                            ...) {
    va_list argp;
    va_start(argp, sig);
    jclass clazz = FindClassExtOrAbort(class_name);
    CallStaticVoidMethodV(
        clazz, GetStaticMethodIDOrAbort(clazz, method_name, sig), argp);
    DeleteLocalRef(clazz);
    va_end(argp);
  }

  void CallStaticVoidMethodOrAbort(const char* class_name,
                                   const char* method_name,
                                   const char* sig,
                                   ...) {
    va_list argp;
    va_start(argp, sig);
    jclass clazz = FindClassExtOrAbort(class_name);
    CallStaticVoidMethodV(
        clazz, GetStaticMethodIDOrAbort(clazz, method_name, sig), argp);
    AbortOnException();
    DeleteLocalRef(clazz);
    va_end(argp);
  }

// Convenience method to create a j[Type]Array from raw, native data. It is
// the responsibility of clients to free the returned array when done with it
// by manually calling |DeleteLocalRef| on it.
#define X(_jtype, _jname)                                                   \
  _jtype##Array New##_jname##ArrayFromRaw(const _jtype* data, jsize size) { \
    SB_DCHECK(data);                                                        \
    SB_DCHECK(size >= 0);                                                   \
    _jtype##Array j_array = New##_jname##Array(size);                       \
    SB_CHECK(j_array) << "Out of memory making new array";                  \
    Set##_jname##ArrayRegion(j_array, 0, size, data);                       \
    return j_array;                                                         \
  }

  X(jboolean, Boolean)
  X(jbyte, Byte)
  X(jchar, Char)
  X(jshort, Short)
  X(jint, Int)
  X(jlong, Long)
  X(jfloat, Float)
  X(jdouble, Double)

#undef X

  jobject ConvertLocalRefToGlobalRef(jobject local) {
    jobject global = NewGlobalRef(local);
    DeleteLocalRef(local);
    return global;
  }

  void AbortOnException() {
    if (!ExceptionCheck()) {
      return;
    }
    ExceptionDescribe();
    SbSystemBreakIntoDebugger();
  }
};

SB_COMPILE_ASSERT(sizeof(JNIEnv) == sizeof(JniEnvExt),
                  JniEnvExt_must_not_add_fields);

}  // namespace shared
}  // namespace android
}  // namespace starboard

#endif  // STARBOARD_ANDROID_SHARED_JNI_ENV_EXT_H_
