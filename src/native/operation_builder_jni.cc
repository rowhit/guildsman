/* Copyright 2016 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "operation_builder_jni.h"

#include <memory>
#include "include/c_api.h"
#include "exception_jni.h"

namespace {
TF_OperationDescription* requireHandle(JNIEnv* env, jlong handle) {
  if (handle == 0) {
    throwException(env, kIllegalStateException,
                   "Operation has already been built");
    return nullptr;
  }
  return reinterpret_cast<TF_OperationDescription*>(handle);
}

bool resolveOutput(JNIEnv* env, jlong op_handle, jint index, TF_Output* out) {
  if (op_handle == 0) {
    throwException(env, kIllegalStateException,
                   "close() was called on the Graph");
    return false;
  }
  out->oper = reinterpret_cast<TF_Operation*>(op_handle);
  out->index = static_cast<int>(index);
  return true;
}

TF_Tensor* requireTensor(JNIEnv* env, jlong handle) {
  if (handle == 0) {
    throwException(env, kIllegalStateException,
                   "close() has been called on the Tensor");
    return nullptr;
  }
  return reinterpret_cast<TF_Tensor*>(handle);
}
}  // namespace

JNIEXPORT jlong JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_allocate(
    JNIEnv* env, jclass clazz, jlong graph_handle, jstring type, jstring name) {
  if (graph_handle == 0) {
    throwException(env, kIllegalStateException,
                   "close() has been called on the Graph");
    return 0;
  }
  TF_Graph* graph = reinterpret_cast<TF_Graph*>(graph_handle);
  const char* op_type = env->GetStringUTFChars(type, nullptr);
  const char* op_name = env->GetStringUTFChars(name, nullptr);
  TF_OperationDescription* d = TF_NewOperation(graph, op_type, op_name);
  env->ReleaseStringUTFChars(name, op_name);
  env->ReleaseStringUTFChars(type, op_type);
  static_assert(sizeof(jlong) >= sizeof(TF_OperationDescription*),
                "Cannot represent a C TF_OperationDescription as a Java long");
  return reinterpret_cast<jlong>(d);
}

JNIEXPORT jlong JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_finish(
    JNIEnv* env, jclass clazz, jlong handle) {
  TF_OperationDescription* d = requireHandle(env, handle);
  if (d == nullptr) return 0;
  TF_Status* status = TF_NewStatus();
  TF_Operation* op = TF_FinishOperation(d, status);
  if (throwExceptionIfNotOK(env, status)) {
    TF_DeleteStatus(status);
    return reinterpret_cast<jlong>(op);
  }
  TF_DeleteStatus(status);
  return 0;
}

JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_addInput(
    JNIEnv* env, jclass clazz, jlong handle, jlong op_handle, jint index) {
  TF_Output out;
  if (!resolveOutput(env, op_handle, index, &out)) return;
  TF_OperationDescription* d = requireHandle(env, handle);
  if (d == nullptr) return;
  TF_AddInput(d, out);
}

JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_addInputList(
    JNIEnv* env, jclass clazz, jlong handle, jlongArray op_handles,
    jintArray indices) {
  TF_OperationDescription* d = requireHandle(env, handle);
  if (d == nullptr) return;
  const size_t n = static_cast<size_t>(env->GetArrayLength(op_handles));
  if (env->GetArrayLength(indices) != n) {
    throwException(env, kIllegalArgumentException,
                   "mismatch in number of Operations (%d) and output indices "
                   "(%d) provided",
                   n, env->GetArrayLength(indices));
    return;
  }
  std::unique_ptr<TF_Output[]> o(new TF_Output[n]);
  jlong* oph = env->GetLongArrayElements(op_handles, nullptr);
  jint* idx = env->GetIntArrayElements(indices, nullptr);
  bool ok = true;
  for (int i = 0; i < n && ok; ++i) {
    ok = resolveOutput(env, oph[i], idx[i], &o[i]);
  }
  env->ReleaseIntArrayElements(indices, idx, JNI_ABORT);
  env->ReleaseLongArrayElements(op_handles, oph, JNI_ABORT);
  if (!ok) return;
  TF_AddInputList(d, o.get(), n);
}

JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_addControlInput(
    JNIEnv* env, jclass clazz, jlong handle, jlong op_handle) {
  if (op_handle == 0) {
    throwException(env, kIllegalStateException,
                   "control input is not valid, "
                   "perhaps the Graph containing it has been closed()?");
    return;
  }
  TF_Operation* control = reinterpret_cast<TF_Operation*>(op_handle);
  TF_OperationDescription* d = requireHandle(env, handle);
  if (d == nullptr) return;
  TF_AddControlInput(d, control);
}

JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_setDevice(
    JNIEnv* env, jclass clazz, jlong handle, jstring device) {
  TF_OperationDescription* d = requireHandle(env, handle);
  if (d == nullptr) return;
  const char* cdevice = env->GetStringUTFChars(device, nullptr);
  TF_SetDevice(d, cdevice);
  env->ReleaseStringUTFChars(device, cdevice);
}

JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_setAttrString(
    JNIEnv* env, jclass clazz, jlong handle, jstring name, jbyteArray value) {
  static_assert(sizeof(jbyte) == 1,
                "Require Java byte to be represented as a single byte");
  TF_OperationDescription* d = requireHandle(env, handle);
  if (d == nullptr) return;
  const char* cname = env->GetStringUTFChars(name, nullptr);
  jbyte* cvalue = env->GetByteArrayElements(value, nullptr);
  TF_SetAttrString(d, cname, cvalue, env->GetArrayLength(value));
  env->ReleaseByteArrayElements(value, cvalue, JNI_ABORT);
  env->ReleaseStringUTFChars(name, cname);
}

#define DEFINE_SET_ATTR_SCALAR(name, jtype, ctype)                           \
  JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_setAttr##name( \
      JNIEnv* env, jclass clazz, jlong handle, jstring name, jtype value) {  \
    static_assert(                                                           \
        sizeof(ctype) >= sizeof(jtype),                                      \
        "Information loss when converting between Java and C types");        \
    TF_OperationDescription* d = requireHandle(env, handle);                 \
    if (d == nullptr) return;                                                \
    const char* cname = env->GetStringUTFChars(name, nullptr);               \
    TF_SetAttr##name(d, cname, static_cast<ctype>(value));                   \
    env->ReleaseStringUTFChars(name, cname);                                 \
  }

#define DEFINE_SET_ATTR_LIST(name, jname, jtype, ctype)            \
  JNIEXPORT void JNICALL                                           \
      Java_com_billpiel_guildsman_OperationBuilderNI_setAttr##name##List(    \
          JNIEnv* env, jclass clazz, jlong handle, jstring name,   \
          jtype##Array value) {                                    \
    TF_OperationDescription* d = requireHandle(env, handle);       \
    if (d == nullptr) return;                                      \
    const char* cname = env->GetStringUTFChars(name, nullptr);     \
    /* Make a copy of the array to paper over any differences */   \
    /* in byte representations of the jtype and ctype         */   \
    /* For example, jint vs TF_DataType.                      */   \
    /* If this copy turns out to be a problem in practice     */   \
    /* can avoid it for many types.                           */   \
    const int n = env->GetArrayLength(value);                      \
    std::unique_ptr<ctype[]> cvalue(new ctype[n]);                 \
    jtype* elems = env->Get##jname##ArrayElements(value, nullptr); \
    for (int i = 0; i < n; ++i) {                                  \
      cvalue[i] = static_cast<ctype>(elems[i]);                    \
    }                                                              \
    TF_SetAttr##name##List(d, cname, cvalue.get(), n);             \
    env->Release##jname##ArrayElements(value, elems, JNI_ABORT);   \
    env->ReleaseStringUTFChars(name, cname);                       \
  }

#define DEFINE_SET_ATTR(name, jname, jtype, ctype) \
  DEFINE_SET_ATTR_SCALAR(name, jtype, ctype)       \
  DEFINE_SET_ATTR_LIST(name, jname, jtype, ctype)

DEFINE_SET_ATTR(Int, Long, jlong, int64_t);
DEFINE_SET_ATTR(Float, Float, jfloat, float);
DEFINE_SET_ATTR(Bool, Boolean, jboolean, unsigned char);
DEFINE_SET_ATTR(Type, Int, jint, TF_DataType);
#undef DEFINE_SET_ATTR
#undef DEFINE_SET_ATTR_LIST
#undef DEFINE_SET_ATTR_SCALAR

JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_setAttrTensor(
    JNIEnv* env, jclass clazz, jlong handle, jstring name,
    jlong tensor_handle) {
  TF_OperationDescription* d = requireHandle(env, handle);
  if (d == nullptr) return;
  TF_Tensor* t = requireTensor(env, tensor_handle);
  if (t == nullptr) return;
  const char* cname = env->GetStringUTFChars(name, nullptr);
  TF_Status* status = TF_NewStatus();
  TF_SetAttrTensor(d, cname, t, status);
  throwExceptionIfNotOK(env, status);
  TF_DeleteStatus(status);
  env->ReleaseStringUTFChars(name, cname);
}

JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_setAttrTensorList(
    JNIEnv* env, jclass clazz, jlong handle, jstring name,
    jlongArray tensor_handles) {
  TF_OperationDescription* d = requireHandle(env, handle);
  if (d == nullptr) return;
  const int n = env->GetArrayLength(tensor_handles);
  std::unique_ptr<TF_Tensor* []> tensors(new TF_Tensor*[n]);
  jlong* jhandles = env->GetLongArrayElements(tensor_handles, nullptr);
  bool ok = true;
  for (int i = 0; i < n && ok; ++i) {
    tensors[i] = requireTensor(env, jhandles[i]);
    ok = !env->ExceptionCheck();
  }
  env->ReleaseLongArrayElements(tensor_handles, jhandles, JNI_ABORT);
  if (!ok) return;

  const char* cname = env->GetStringUTFChars(name, nullptr);
  TF_Status* status = TF_NewStatus();
  TF_SetAttrTensorList(d, cname, tensors.get(), n, status);
  throwExceptionIfNotOK(env, status);
  TF_DeleteStatus(status);
  env->ReleaseStringUTFChars(name, cname);
}

JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_setAttrShape(
    JNIEnv* env, jclass clazz, jlong handle, jstring name, jlongArray shape,
    jint num_dims) {
  TF_OperationDescription* d = requireHandle(env, handle);
  if (d == nullptr) return;
  std::unique_ptr<int64_t[]> cvalue;
  // num_dims and env->GetArrayLength(shape) are assumed to be consistent.
  // i.e., either num_dims < 0 or num_dims == env->GetArrayLength(shape).
  if (num_dims > 0) {
    cvalue.reset(new int64_t[num_dims]);
    jlong* elems = env->GetLongArrayElements(shape, nullptr);
    for (int i = 0; i < num_dims; ++i) {
      cvalue[i] = static_cast<int64_t>(elems[i]);
    }
    env->ReleaseLongArrayElements(shape, elems, JNI_ABORT);
  }
  const char* cname = env->GetStringUTFChars(name, nullptr);
  TF_SetAttrShape(d, cname, cvalue.get(), static_cast<int>(num_dims));
  env->ReleaseStringUTFChars(name, cname);
}

JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_setAttrStringList(
    JNIEnv* env, jclass object, jlong handle, jstring name,
    jobjectArray values) {
  TF_OperationDescription* d = requireHandle(env, handle);
  if (d == nullptr) return;
  const char* cname = env->GetStringUTFChars(name, nullptr);
  int num_values = env->GetArrayLength(values);
  static_assert(sizeof(jbyte) == 1,
                "Require Java byte to be represented as a single byte");
  std::unique_ptr<jbyteArray[]> jarrays(new jbyteArray[num_values]);
  std::unique_ptr<jbyte* []> jvalues(new jbyte*[num_values]);
  std::unique_ptr<void* []> cvalues(new void*[num_values]);
  std::unique_ptr<size_t[]> lengths(new size_t[num_values]);

  for (int i = 0; i < num_values; ++i) {
    jbyteArray v =
        static_cast<jbyteArray>(env->GetObjectArrayElement(values, i));
    jarrays[i] = v;
    jvalues[i] = env->GetByteArrayElements(v, nullptr);
    cvalues[i] = jvalues[i];
    lengths[i] = static_cast<size_t>(env->GetArrayLength(v));
  }
  TF_SetAttrStringList(d, cname, cvalues.get(), lengths.get(), num_values);
  for (int i = 0; i < num_values; ++i) {
    env->ReleaseByteArrayElements(jarrays[i], jvalues[i], JNI_ABORT);
  }
  env->ReleaseStringUTFChars(name, cname);
}

JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_setAttrShapeList(
    JNIEnv* env, jclass object, jlong handle, jstring name, jobjectArray shapes, jintArray num_dims, jint num_shapes) {
  TF_OperationDescription *d = requireHandle(env, handle);
  if (d == nullptr) return;
  std::unique_ptr<int[]> c_num_dims;
  std::unique_ptr<int64_t*[]> c_shapes;
  int c_num_shapes = static_cast<int>(num_shapes);
  // num_dims[i] and env->GetArrayLength(shapes[i]) are assumed to be consistent.
  // i.e., either num_dims[i] < 0 or num_dims[i] == env->GetArrayLength(shapes[i]).
  if (c_num_shapes > 0) {
    c_num_dims.reset(new int[c_num_shapes]);
    c_shapes.reset(new int64_t*[c_num_shapes]);
    jint *num_dims_elems = env->GetIntArrayElements(num_dims, nullptr);
    for (int j = 0; j < c_num_shapes; ++j) {
      c_num_dims[j] = static_cast<int>(num_dims_elems[j]);
      if (c_num_dims[j] > -1) {
        c_shapes[j] = new int64_t[c_num_dims[j]];
        jlongArray shapes_elems = (jlongArray) env->GetObjectArrayElement(shapes, j);
        jlong *shape_elems = env->GetLongArrayElements(shapes_elems, nullptr);
        for (int i = 0; i < c_num_dims[j]; ++i) {
          c_shapes[j][i] = static_cast<int64_t>(shape_elems[i]);
        }
        env->ReleaseLongArrayElements(shapes_elems, shape_elems, JNI_ABORT);
      } else {
        c_shapes[j] = new int64_t[0];
      }
    }
    env->ReleaseIntArrayElements(num_dims, num_dims_elems, JNI_ABORT);
  }
  const char *cname = env->GetStringUTFChars(name, nullptr);
  TF_SetAttrShapeList(d, cname, c_shapes.get(), c_num_dims.get(), c_num_shapes);
  env->ReleaseStringUTFChars(name, cname);
}

JNIEXPORT void JNICALL Java_com_billpiel_guildsman_OperationBuilderNI_setAttrProto
(JNIEnv* env, jclass object, jlong handle, jstring name, jbyteArray value) {
  static_assert(sizeof(jbyte) == 1, "Require Java byte to be represented as a single byte"); 
  TF_OperationDescription *d = requireHandle(env, handle);
  if (d == nullptr) return;
  const char *c_name = env->GetStringUTFChars(name, nullptr);
  jbyte *c_value = env->GetByteArrayElements(value, nullptr);
  std::unique_ptr<TF_Status, decltype(&TF_DeleteStatus)> status(TF_NewStatus(), TF_DeleteStatus);
  TF_SetAttrValueProto(d, c_name, c_value, static_cast<size_t>(env->GetArrayLength(value)), status.get());
  env->ReleaseByteArrayElements(value, c_value, JNI_ABORT);
  env->ReleaseStringUTFChars(name, c_name);
  CHECK_STATUS(env, status.get(), void());
}
