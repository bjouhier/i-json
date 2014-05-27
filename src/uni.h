// borrowed uni compatibility templates from https://github.com/laverdet/node-fibers/blob/master/src/fibers.cc#L24-124
// Handle legacy V8 API
#include <uv.h>
#include <node_object_wrap.h>
namespace uni {
#if NODE_MODULE_VERSION >= 0x000D
  typedef void CallbackType;
  typedef v8::FunctionCallbackInfo<v8::Value> FunctionCallbackInfo;
  typedef v8::Local<Value> BufferType;
# define UNI_RETURN(scope, args, res) { args.GetReturnValue().Set(res); return; }
# define UNI_THROW(ex) { ThrowException(ex); return; }
# define UNI_SCOPE(scope) HandleScope scope(Isolate::GetCurrent()) 
  template <class T>
  Persistent<T> New(Isolate* isolate, Handle<T> handle) {
    return Persistent<T>::New(isolate, handle);
  }
  template <class T>
  void Reset(Persistent<T>& persistent, Handle<T> handle) {
    persistent.Reset(Isolate::GetCurrent(), handle);
  }
  template <class T>
  Handle<T> Deref(Persistent<T>& handle) {
    return Handle<T>::New(Isolate::GetCurrent(), handle);
  }
  template <class T>
  Local<T> HandleToLocal(Handle<T> handle) {
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
# define UNI_THROW(ex) return ThrowException(ex)
# define UNI_SCOPE(scope) HandleScope scope
  template <class T>
  Persistent<T> New(Isolate* isolate, Handle<T> handle) {
    return Persistent<T>::New(handle);
  }
  template <class T>
  void Reset(Persistent<T>& persistent, Handle<T> handle) {
    persistent = Persistent<T>::New(handle);
  }
  template <class T>
  Handle<T> Deref(Persistent<T>& handle) {
    return Local<T>::New(handle);
  }
  template <class T>
  Local<T> HandleToLocal(Handle<T> handle) {
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
