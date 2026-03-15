#include <napi.h>
#include "library/cc/engine_builder.h"
#include "library/cc/engine.h"
#include "library/cc/stream_client.h"
#include "library/cc/stream_prototype.h"
#include "library/cc/stream.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/buffer/buffer_impl.h"

// These are required by the version library in "source/common/version/version.h".
extern const char build_scm_revision[];
extern const char build_scm_status[];
const char build_scm_revision[] = "0";
const char build_scm_status[] = "nodejs";

class EnvoyStreamWrapper : public Napi::ObjectWrap<EnvoyStreamWrapper> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  EnvoyStreamWrapper(const Napi::CallbackInfo& info);

  Napi::Value SendHeaders(const Napi::CallbackInfo& info);
  Napi::Value SendData(const Napi::CallbackInfo& info);
  Napi::Value Close(const Napi::CallbackInfo& info);
  Napi::Value Cancel(const Napi::CallbackInfo& info);

  Envoy::Platform::StreamSharedPtr stream_;
  static Napi::FunctionReference constructor;
};

Napi::FunctionReference EnvoyStreamWrapper::constructor;

class EnvoyStreamPrototypeWrapper : public Napi::ObjectWrap<EnvoyStreamPrototypeWrapper> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  EnvoyStreamPrototypeWrapper(const Napi::CallbackInfo& info);

  Napi::Value Start(const Napi::CallbackInfo& info);

  Envoy::Platform::StreamPrototypeSharedPtr prototype_;
  static Napi::FunctionReference constructor;
};

Napi::FunctionReference EnvoyStreamPrototypeWrapper::constructor;

class EnvoyStreamClientWrapper : public Napi::ObjectWrap<EnvoyStreamClientWrapper> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  EnvoyStreamClientWrapper(const Napi::CallbackInfo& info);

  Napi::Value NewStreamPrototype(const Napi::CallbackInfo& info);

  Envoy::Platform::StreamClientSharedPtr client_;
  static Napi::FunctionReference constructor;
};

Napi::FunctionReference EnvoyStreamClientWrapper::constructor;

class EnvoyEngineWrapper : public Napi::ObjectWrap<EnvoyEngineWrapper> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  EnvoyEngineWrapper(const Napi::CallbackInfo& info);

  void Terminate(const Napi::CallbackInfo& info);
  Napi::Value DumpStats(const Napi::CallbackInfo& info);
  Napi::Value GetStreamClient(const Napi::CallbackInfo& info);

  Envoy::Platform::EngineSharedPtr engine_;
  static Napi::FunctionReference constructor;
};

Napi::FunctionReference EnvoyEngineWrapper::constructor;

class EnvoyEngineBuilderWrapper : public Napi::ObjectWrap<EnvoyEngineBuilderWrapper> {
public:
  static Napi::Object Init(Napi::Env env, Napi::Object exports);
  EnvoyEngineBuilderWrapper(const Napi::CallbackInfo& info);

  Napi::Value SetLogLevel(const Napi::CallbackInfo& info);
  Napi::Value SetOnEngineRunning(const Napi::CallbackInfo& info);
  Napi::Value Build(const Napi::CallbackInfo& info);

private:
  Envoy::Platform::EngineBuilder builder_;
};

// --- EnvoyStreamWrapper Implementation ---

Napi::Object EnvoyStreamWrapper::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func =
      DefineClass(env, "EnvoyStream",
                  {
                      InstanceMethod("sendHeaders", &EnvoyStreamWrapper::SendHeaders),
                      InstanceMethod("sendData", &EnvoyStreamWrapper::SendData),
                      InstanceMethod("close", &EnvoyStreamWrapper::Close),
                      InstanceMethod("cancel", &EnvoyStreamWrapper::Cancel),
                  });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("EnvoyStream", func);
  return exports;
}

EnvoyStreamWrapper::EnvoyStreamWrapper(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<EnvoyStreamWrapper>(info) {}

Napi::Value EnvoyStreamWrapper::SendHeaders(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[0].IsObject() || !info[1].IsBoolean()) {
    Napi::TypeError::New(env, "Expected (headers: object, endStream: boolean)")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  auto headers_obj = info[0].As<Napi::Object>();
  bool end_stream = info[1].As<Napi::Boolean>();

  auto headers = Envoy::Http::RequestHeaderMapImpl::create();
  Napi::Array names = headers_obj.GetPropertyNames();
  for (uint32_t i = 0; i < names.Length(); i++) {
    std::string key = names.Get(i).As<Napi::String>();
    std::string val = headers_obj.Get(key).As<Napi::String>();
    headers->addCopy(Envoy::Http::LowerCaseString(key), val);
  }

  stream_->sendHeaders(std::move(headers), end_stream);
  return info.This();
}

Napi::Value EnvoyStreamWrapper::SendData(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsBuffer()) {
    Napi::TypeError::New(env, "Expected Buffer").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto buf = info[0].As<Napi::Buffer<uint8_t>>();
  auto envoy_buf = std::make_unique<Envoy::Buffer::OwnedImpl>(buf.Data(), buf.Length());
  stream_->sendData(std::move(envoy_buf));
  return info.This();
}

Napi::Value EnvoyStreamWrapper::Close(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() > 0 && info[0].IsBuffer()) {
    auto buf = info[0].As<Napi::Buffer<uint8_t>>();
    auto envoy_buf = std::make_unique<Envoy::Buffer::OwnedImpl>(buf.Data(), buf.Length());
    stream_->close(std::move(envoy_buf));
  } else {
    auto envoy_buf = std::make_unique<Envoy::Buffer::OwnedImpl>();
    stream_->close(std::move(envoy_buf));
  }
  return env.Null();
}

Napi::Value EnvoyStreamWrapper::Cancel(const Napi::CallbackInfo& info) {
  stream_->cancel();
  return info.Env().Null();
}

// --- EnvoyStreamPrototypeWrapper Implementation ---

Napi::Object EnvoyStreamPrototypeWrapper::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func =
      DefineClass(env, "EnvoyStreamPrototype",
                  {
                      InstanceMethod("start", &EnvoyStreamPrototypeWrapper::Start),
                  });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("EnvoyStreamPrototype", func);
  return exports;
}

EnvoyStreamPrototypeWrapper::EnvoyStreamPrototypeWrapper(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<EnvoyStreamPrototypeWrapper>(info) {}

Napi::Value EnvoyStreamPrototypeWrapper::Start(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsObject()) {
    Napi::TypeError::New(env, "Expected callbacks object").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto callbacks_obj = info[0].As<Napi::Object>();
  Envoy::EnvoyStreamCallbacks callbacks;

  auto on_headers_tsfn = std::make_shared<Napi::ThreadSafeFunction>();
  bool has_on_headers = false;
  if (callbacks_obj.Has("onHeaders")) {
    *on_headers_tsfn = Napi::ThreadSafeFunction::New(
        env, callbacks_obj.Get("onHeaders").As<Napi::Function>(), "onHeaders", 0, 1);
    has_on_headers = true;
  }

  auto on_data_tsfn = std::make_shared<Napi::ThreadSafeFunction>();
  bool has_on_data = false;
  if (callbacks_obj.Has("onData")) {
    *on_data_tsfn = Napi::ThreadSafeFunction::New(
        env, callbacks_obj.Get("onData").As<Napi::Function>(), "onData", 0, 1);
    has_on_data = true;
  }

  auto on_complete_tsfn = std::make_shared<Napi::ThreadSafeFunction>();
  bool has_on_complete = false;
  if (callbacks_obj.Has("onComplete")) {
    *on_complete_tsfn = Napi::ThreadSafeFunction::New(
        env, callbacks_obj.Get("onComplete").As<Napi::Function>(), "onComplete", 0, 1);
    has_on_complete = true;
  }

  auto on_error_tsfn = std::make_shared<Napi::ThreadSafeFunction>();
  bool has_on_error = false;
  if (callbacks_obj.Has("onError")) {
    *on_error_tsfn = Napi::ThreadSafeFunction::New(
        env, callbacks_obj.Get("onError").As<Napi::Function>(), "onError", 0, 1);
    has_on_error = true;
  }

  auto on_cancel_tsfn = std::make_shared<Napi::ThreadSafeFunction>();
  bool has_on_cancel = false;
  if (callbacks_obj.Has("onCancel")) {
    *on_cancel_tsfn = Napi::ThreadSafeFunction::New(
        env, callbacks_obj.Get("onCancel").As<Napi::Function>(), "onCancel", 0, 1);
    has_on_cancel = true;
  }

  auto release_all = [on_headers_tsfn, has_on_headers, on_data_tsfn, has_on_data, on_complete_tsfn,
                      has_on_complete, on_error_tsfn, has_on_error, on_cancel_tsfn,
                      has_on_cancel]() {
    if (has_on_headers)
      on_headers_tsfn->Release();
    if (has_on_data)
      on_data_tsfn->Release();
    if (has_on_complete)
      on_complete_tsfn->Release();
    if (has_on_error)
      on_error_tsfn->Release();
    if (has_on_cancel)
      on_cancel_tsfn->Release();
  };

  callbacks.on_headers_ = [on_headers_tsfn,
                           has_on_headers](const Envoy::Http::ResponseHeaderMap& headers,
                                           bool end_stream, envoy_stream_intel intel) {
    if (!has_on_headers)
      return;
    auto headers_map = std::make_shared<std::map<std::string, std::string>>();
    headers.iterate([headers_map](const Envoy::Http::HeaderEntry& entry) {
      (*headers_map)[std::string(entry.key().getStringView())] =
          std::string(entry.value().getStringView());
      return Envoy::Http::HeaderMap::Iterate::Continue;
    });

    on_headers_tsfn->BlockingCall(
        [headers_map, end_stream](Napi::Env env, Napi::Function jsCallback) {
          Napi::Object js_headers = Napi::Object::New(env);
          for (auto const& [key, val] : *headers_map) {
            js_headers.Set(key, val);
          }
          jsCallback.Call({js_headers, Napi::Boolean::New(env, end_stream)});
        });
  };

  callbacks.on_data_ = [on_data_tsfn, has_on_data](const Envoy::Buffer::Instance& data,
                                                   uint64_t length, bool end_stream,
                                                   envoy_stream_intel intel) {
    if (!has_on_data)
      return;
    std::string data_str = data.toString();
    on_data_tsfn->BlockingCall([data_str, end_stream](Napi::Env env, Napi::Function jsCallback) {
      Napi::Buffer<char> js_buffer =
          Napi::Buffer<char>::Copy(env, data_str.c_str(), data_str.length());
      jsCallback.Call({js_buffer, Napi::Boolean::New(env, end_stream)});
    });
  };

  callbacks.on_complete_ = [on_complete_tsfn, has_on_complete, release_all](
                               envoy_stream_intel intel, envoy_final_stream_intel final_intel) {
    if (has_on_complete) {
      on_complete_tsfn->BlockingCall(
          [](Napi::Env env, Napi::Function jsCallback) { jsCallback.Call({}); });
    }
    release_all();
  };

  callbacks.on_error_ = [on_error_tsfn, has_on_error,
                         release_all](const Envoy::EnvoyError& error, envoy_stream_intel intel,
                                      envoy_final_stream_intel final_intel) {
    if (has_on_error) {
      std::string msg = error.message_;
      int code = error.error_code_;
      on_error_tsfn->BlockingCall([msg, code](Napi::Env env, Napi::Function jsCallback) {
        Napi::Object err_obj = Napi::Object::New(env);
        err_obj.Set("message", msg);
        err_obj.Set("code", code);
        jsCallback.Call({err_obj});
      });
    }
    release_all();
  };

  callbacks.on_cancel_ = [on_cancel_tsfn, has_on_cancel, release_all](
                             envoy_stream_intel intel, envoy_final_stream_intel final_intel) {
    if (has_on_cancel) {
      on_cancel_tsfn->BlockingCall(
          [](Napi::Env env, Napi::Function jsCallback) { jsCallback.Call({}); });
    }
    release_all();
  };

  auto stream = prototype_->start(std::move(callbacks));
  Napi::Object obj = EnvoyStreamWrapper::constructor.New({});
  EnvoyStreamWrapper* wrapper = EnvoyStreamWrapper::Unwrap(obj);
  wrapper->stream_ = stream;
  return obj;
}

// --- EnvoyStreamClientWrapper Implementation ---

Napi::Object EnvoyStreamClientWrapper::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(
      env, "EnvoyStreamClient",
      {
          InstanceMethod("newStreamPrototype", &EnvoyStreamClientWrapper::NewStreamPrototype),
      });
  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();
  exports.Set("EnvoyStreamClient", func);
  return exports;
}

EnvoyStreamClientWrapper::EnvoyStreamClientWrapper(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<EnvoyStreamClientWrapper>(info) {}

Napi::Value EnvoyStreamClientWrapper::NewStreamPrototype(const Napi::CallbackInfo& info) {
  auto proto = client_->newStreamPrototype();
  Napi::Object obj = EnvoyStreamPrototypeWrapper::constructor.New({});
  EnvoyStreamPrototypeWrapper* wrapper = EnvoyStreamPrototypeWrapper::Unwrap(obj);
  wrapper->prototype_ = proto;
  return obj;
}

// --- EnvoyEngineWrapper Implementation ---

Napi::Object EnvoyEngineWrapper::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func =
      DefineClass(env, "EnvoyEngine",
                  {
                      InstanceMethod("terminate", &EnvoyEngineWrapper::Terminate),
                      InstanceMethod("dumpStats", &EnvoyEngineWrapper::DumpStats),
                      InstanceMethod("getStreamClient", &EnvoyEngineWrapper::GetStreamClient),
                  });

  constructor = Napi::Persistent(func);
  constructor.SuppressDestruct();

  exports.Set("EnvoyEngine", func);
  return exports;
}

EnvoyEngineWrapper::EnvoyEngineWrapper(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<EnvoyEngineWrapper>(info) {}

void EnvoyEngineWrapper::Terminate(const Napi::CallbackInfo& info) {
  if (engine_) {
    engine_->terminate();
  }
}

Napi::Value EnvoyEngineWrapper::DumpStats(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!engine_) {
    return Napi::String::New(env, "");
  }
  return Napi::String::New(env, engine_->dumpStats());
}

Napi::Value EnvoyEngineWrapper::GetStreamClient(const Napi::CallbackInfo& info) {
  auto client = engine_->streamClient();
  Napi::Object obj = EnvoyStreamClientWrapper::constructor.New({});
  EnvoyStreamClientWrapper* wrapper = EnvoyStreamClientWrapper::Unwrap(obj);
  wrapper->client_ = client;
  return obj;
}

// --- EnvoyEngineBuilderWrapper Implementation ---

Napi::Object EnvoyEngineBuilderWrapper::Init(Napi::Env env, Napi::Object exports) {
  Napi::Function func = DefineClass(
      env, "EnvoyEngineBuilder",
      {
          InstanceMethod("setLogLevel", &EnvoyEngineBuilderWrapper::SetLogLevel),
          InstanceMethod("setOnEngineRunning", &EnvoyEngineBuilderWrapper::SetOnEngineRunning),
          InstanceMethod("build", &EnvoyEngineBuilderWrapper::Build),
      });

  exports.Set("EnvoyEngineBuilder", func);
  return exports;
}

EnvoyEngineBuilderWrapper::EnvoyEngineBuilderWrapper(const Napi::CallbackInfo& info)
    : Napi::ObjectWrap<EnvoyEngineBuilderWrapper>(info) {}

Napi::Value EnvoyEngineBuilderWrapper::SetLogLevel(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "Number expected").ThrowAsJavaScriptException();
    return env.Null();
  }

  int level = info[0].As<Napi::Number>().Int32Value();
  builder_.setLogLevel(static_cast<Envoy::Logger::Logger::Levels>(level));
  return info.This();
}

Napi::Value EnvoyEngineBuilderWrapper::SetOnEngineRunning(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsFunction()) {
    Napi::TypeError::New(env, "Function expected").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto tsfn =
      Napi::ThreadSafeFunction::New(env, info[0].As<Napi::Function>(), "OnEngineRunning", 0, 1);
  builder_.setOnEngineRunning([tsfn]() {
    tsfn.BlockingCall([](Napi::Env env, Napi::Function jsCallback) { jsCallback.Call({}); });
    tsfn.Release();
  });

  return info.This();
}

Napi::Value EnvoyEngineBuilderWrapper::Build(const Napi::CallbackInfo& info) {
  auto engine = builder_.build();
  Napi::Object obj = EnvoyEngineWrapper::constructor.New({});
  EnvoyEngineWrapper* wrapper = EnvoyEngineWrapper::Unwrap(obj);
  wrapper->engine_ = engine;
  return obj;
}

// --- Module Initialization ---

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  EnvoyStreamWrapper::Init(env, exports);
  EnvoyStreamPrototypeWrapper::Init(env, exports);
  EnvoyStreamClientWrapper::Init(env, exports);
  EnvoyEngineWrapper::Init(env, exports);
  EnvoyEngineBuilderWrapper::Init(env, exports);
  return exports;
}

NODE_API_MODULE(envoy_mobile_node, Init)
