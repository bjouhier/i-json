// borrowed uni compatibility templates from https://github.com/laverdet/node-fibers/blob/master/src/fibers.cc#L24-124
// Handle legacy V8 API
#include <uv.h>
#include <node_object_wrap.h>
namespace uni {
#if NODE_MODULE_VERSION >= 14
  typedef void CallbackType;
  typedef v8::FunctionCallbackInfo<v8::Value> FunctionCallbackInfo;
  typedef v8::Local<Value> BufferType;
# define UNI_RETURN(scope, args, res) { args.GetReturnValue().Set(res); return; }
# define UNI_SCOPE(scope) HandleScope scope(Isolate::GetCurrent()) 
# define UNI_THROW(isolate, ex) { isolate->ThrowException(ex); return; }

  inline Local<String> NewString(Isolate* isolate, const char* str, int len = -1) {
    return String::NewFromUtf8(isolate, str, String::kNormalString, len);
  }
  inline Local<String> NewSymbol(Isolate* isolate, const char* str, int len = -1) {
    return String::NewFromUtf8(isolate, str, String::kNormalString, len);
  }
  inline Local<Array> NewArray(Isolate* isolate, int len) {
    return Array::New(isolate, len);
  }
  inline Local<Integer> NewInteger(Isolate* isolate, int val) {
    return Integer::New(isolate, val);
  }
  inline Local<Number> NewNumber(Isolate* isolate, double val) {
    return Number::New(isolate, val);
  }
  inline Local<Object> NewObject(Isolate* isolate) {
    return Object::New(isolate);
  }
  inline Local<Value> NewValue(Isolate* isolate, Handle<Value> val) {
    return Local<Value>::New(isolate, val);
  }
  inline Local<FunctionTemplate> NewFunctionTemplate(Isolate* isolate, FunctionCallback callback) {
    return FunctionTemplate::New(isolate, callback);
  }
  inline Handle<Boolean> True(Isolate* isolate) {
    return v8::True(isolate);
  }
  inline Handle<Boolean> False(Isolate* isolate) {
    return v8::False(isolate);
  }
  inline Handle<Primitive> Null(Isolate* isolate) {
    return v8::Null(isolate);
  }
  inline Handle<Primitive> Undefined(Isolate* isolate) {
    return v8::Undefined(isolate);
  }
  inline Local<Context> GetCurrentContext(Isolate* isolate) {
    return isolate->GetCurrentContext();
  }
  inline Handle<Value> MakeCallback(Isolate* isolate, Handle<Object> target, Handle<Function> fn, int argc, Handle<Value>* argv) {
    return node::MakeCallback(isolate, target, fn, argc, argv);
  }
  template <class T>
  inline void Dispose(Isolate* isolate, Persistent<T>& handle) {
    handle.Reset();
  }
  template <class T>
  inline Handle<T> Deref(Isolate* isolate, Persistent<T>& handle) {
    return Local<T>::New(isolate, handle);
  }
  template <class T>
  inline Persistent<T> New(Isolate* isolate, Handle<T> handle) {
    return Persistent<T>::New(isolate, handle);
  }
  template <class T>
  inline void Reset(Isolate* isolate, Persistent<T>& persistent, Handle<T> handle) {
    persistent.Reset(isolate, handle);
  }
  template <class T>
  inline Local<T> HandleToLocal(Handle<T> handle) {
    return handle;
  }
  inline Handle<Value> BufferToHandle(BufferType buf) {
    return buf;
  }
  inline Local<Date> DateCast(Local<Value> date) {
    return Local<Date>::Cast(date);
  }
#else
  typedef Handle<Value> CallbackType;
  typedef Arguments FunctionCallbackInfo;
  typedef node::Buffer* BufferType;
# define UNI_RETURN(scope, args, res) return scope.Close(res)
# define UNI_SCOPE(scope) HandleScope scope
# define UNI_THROW(isolate, ex) return ThrowException(ex)
  template <class T>
  inline void Dispose(Isolate* isolate, Persistent<T>& handle) {
    handle.Dispose();
  }
  template <class T>
  inline Handle<T> Deref(Isolate* isolate, Persistent<T>& handle) {
    return Local<T>::New(handle);
  }
  inline Local<String> NewString(Isolate* isolate, const char* str, int len = -1) {
    return String::New(str, len);
  }
  inline Local<String> NewSymbol(Isolate* isolate, const char* str, int len = -1) {
    return String::NewSymbol(str, len);
  }
  inline Local<Array> NewArray(Isolate* isolate, int len) {
    return Array::New(len);
  }
  inline Local<Integer> NewInteger(Isolate* isolate, int val) {
    return Integer::New(val);
  }
  inline Local<Number> NewNumber(Isolate* isolate, double val) {
    return Number::New(val);
  }
  inline Local<Object> NewObject(Isolate* isolate) {
    return Object::New();
  }
  inline Local<Value> NewValue(Isolate* isolate, Handle<Value> val) {
    return Local<Value>::New(val);
  }
  inline Local<FunctionTemplate> NewFunctionTemplate(Isolate* isolate, InvocationCallback callback) {
    return FunctionTemplate::New(callback);
  }
  inline Handle<Boolean> True(Isolate* isolate) {
    return v8::True();
  }
  inline Handle<Boolean> False(Isolate* isolate) {
    return v8::False();
  }
  inline Handle<Primitive> Null(Isolate* isolate) {
    return v8::Null();
  }
  inline Handle<Primitive> Undefined(Isolate* isolate) {
    return v8::Undefined();
  }
  inline Local<Context> GetCurrentContext(Isolate* isolate) {
    return Context::GetCurrent();
  }
  inline Handle<Value> MakeCallback(Isolate* isolate, Handle<Object> target, Handle<Function> fn, int argc, Handle<Value>* argv) {
    return node::MakeCallback(target, fn, argc, argv);
  }
  template <class T>
  inline Persistent<T> New(Isolate* isolate, Handle<T> handle) {
    return Persistent<T>::New(handle);
  }
  template <class T>
  inline void Reset(Isolate* isolate, Persistent<T>& persistent, Handle<T> handle) {
    persistent = Persistent<T>::New(handle);
  }
  template <class T>
  inline Local<T> HandleToLocal(Handle<T> handle) {
    return Local<T>::New(handle);
  }
  inline Handle<Value> BufferToHandle(BufferType buf) {
    return buf->handle_;
  }
  inline Local<Date> DateCast(Local<Value> date) {
    return Date::Cast(*date);
  }
#endif
}
