// Copyright (c) 2017 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/renderer/web_worker_observer.h"

#include "base/lazy_instance.h"
#include "base/threading/thread_local.h"
#include "shell/common/api/electron_bindings.h"
#include "shell/common/api/event_emitter_caller.h"
#include "shell/common/asar/asar_util.h"
#include "shell/common/node_bindings.h"
#include "shell/common/node_includes.h"

namespace electron {

namespace {

static base::LazyInstance<
    base::ThreadLocalPointer<WebWorkerObserver>>::DestructorAtExit lazy_tls =
    LAZY_INSTANCE_INITIALIZER;

}  // namespace

// static
WebWorkerObserver* WebWorkerObserver::GetCurrent() {
  WebWorkerObserver* self = lazy_tls.Pointer()->Get();
  return self ? self : new WebWorkerObserver;
}

WebWorkerObserver::WebWorkerObserver()
    : node_bindings_(
          NodeBindings::Create(NodeBindings::BrowserEnvironment::WORKER)),
      electron_bindings_(new ElectronBindings(node_bindings_->uv_loop())) {
  lazy_tls.Pointer()->Set(this);
}

WebWorkerObserver::~WebWorkerObserver() {
  lazy_tls.Pointer()->Set(nullptr);
  node::FreeEnvironment(node_bindings_->uv_env());
  asar::ClearArchives();
}

void WebWorkerObserver::ContextCreated(v8::Local<v8::Context> worker_context) {
  v8::Context::Scope context_scope(worker_context);

  // Start the embed thread.
  node_bindings_->PrepareMessageLoop();

  // Setup node environment for each window.
  v8::Local<v8::Context> context = node::MaybeInitializeContext(worker_context);
  DCHECK(!context.IsEmpty());
  node::Environment* env = node_bindings_->CreateEnvironment(context);

  // Add Electron extended APIs.
  electron_bindings_->BindTo(env->isolate(), env->process_object());

  // Load everything.
  node_bindings_->LoadEnvironment(env);

  // Make uv loop being wrapped by window context.
  node_bindings_->set_uv_env(env);

  // Give the node loop a run to make sure everything is ready.
  node_bindings_->RunMessageLoop();
}

void WebWorkerObserver::ContextWillDestroy(v8::Local<v8::Context> context) {
  node::Environment* env = node::Environment::GetCurrent(context);
  if (env)
    mate::EmitEvent(env->isolate(), env->process_object(), "exit");

  delete this;
}

}  // namespace electron
