/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 *
 * Copyright 2016 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "wasm/WasmPI.h"

#include "builtin/Promise.h"
#include "debugger/DebugAPI.h"
#include "debugger/Debugger.h"
#include "jit/JitRuntime.h"
#include "jit/MIRGenerator.h"
#include "js/CallAndConstruct.h"
#include "js/Printf.h"
#include "vm/Iteration.h"
#include "vm/JSContext.h"
#include "vm/JSObject.h"
#include "vm/NativeObject.h"
#include "vm/PromiseObject.h"
#include "wasm/WasmConstants.h"
#include "wasm/WasmContext.h"
#include "wasm/WasmFeatures.h"
#include "wasm/WasmGenerator.h"
#include "wasm/WasmIonCompile.h"  // IonPlatformSupport
#include "wasm/WasmValidate.h"

#include "vm/JSObject-inl.h"
#include "wasm/WasmGcObject-inl.h"
#include "wasm/WasmInstance-inl.h"

#ifdef JS_CODEGEN_ARM
#  include "jit/arm/Simulator-arm.h"
#endif

#ifdef JS_CODEGEN_ARM64
#  include "jit/arm64/vixl/Simulator-vixl.h"
#endif

#ifdef JS_CODEGEN_RISCV64
#  include "jit/riscv64/Simulator-riscv64.h"
#endif

#ifdef XP_WIN
#  include "util/WindowsWrapper.h"
#endif

using namespace js;
using namespace js::jit;

#ifdef ENABLE_WASM_JSPI
namespace js::wasm {

SuspenderObjectData::SuspenderObjectData(void* stackMemory)
    : stackMemory_(stackMemory),
      suspendableFP_(nullptr),
      suspendableSP_(static_cast<uint8_t*>(stackMemory) +
                     SuspendableStackPlusRedZoneSize),
      state_(SuspenderState::Initial),
      suspendedBy_(nullptr) {}

void SuspenderObjectData::releaseStackMemory() {
  js_free(stackMemory_);
  stackMemory_ = nullptr;
}

#  if defined(_WIN32)
// On WIN64, the Thread Information Block stack limits has to be modified to
// avoid failures on SP checks.
void SuspenderObjectData::updateTIBStackFields() {
  _NT_TIB* tib = reinterpret_cast<_NT_TIB*>(::NtCurrentTeb());
  savedStackBase_ = tib->StackBase;
  savedStackLimit_ = tib->StackLimit;
  uintptr_t stack_limit = (uintptr_t)stackMemory_;
  uintptr_t stack_base = stack_limit + SuspendableStackPlusRedZoneSize;
  tib->StackBase = (void*)stack_base;
  tib->StackLimit = (void*)stack_limit;
}

void SuspenderObjectData::restoreTIBStackFields() {
  _NT_TIB* tib = reinterpret_cast<_NT_TIB*>(::NtCurrentTeb());
  tib->StackBase = savedStackBase_;
  tib->StackLimit = savedStackLimit_;
}
#  endif

#  ifdef JS_SIMULATOR_ARM64
void SuspenderObjectData::switchSimulatorToMain() {
  auto* sim = Simulator::Current();
  suspendableSP_ = (void*)sim->xreg(Registers::sp, vixl::Reg31IsStackPointer);
  suspendableFP_ = (void*)sim->xreg(Registers::fp);
  sim->set_xreg(Registers::sp, (int64_t)mainSP_, vixl::Debugger::LogRegWrites,
                vixl::Reg31IsStackPointer);
  sim->set_xreg(Registers::fp, (int64_t)mainFP_);
}

void SuspenderObjectData::switchSimulatorToSuspendable() {
  auto* sim = Simulator::Current();
  mainSP_ = (void*)sim->xreg(Registers::sp, vixl::Reg31IsStackPointer);
  mainFP_ = (void*)sim->xreg(Registers::fp);
  sim->set_xreg(Registers::sp, (int64_t)suspendableSP_,
                vixl::Debugger::LogRegWrites, vixl::Reg31IsStackPointer);
  sim->set_xreg(Registers::fp, (int64_t)suspendableFP_);
}
#  endif

#  ifdef JS_SIMULATOR_ARM
void SuspenderObjectData::switchSimulatorToMain() {
  suspendableSP_ = (void*)Simulator::Current()->get_register(Simulator::sp);
  suspendableFP_ = (void*)Simulator::Current()->get_register(Simulator::fp);
  Simulator::Current()->set_register(Simulator::sp, (int)mainSP_);
  Simulator::Current()->set_register(Simulator::fp, (int)mainFP_);
}

void SuspenderObjectData::switchSimulatorToSuspendable() {
  mainSP_ = (void*)Simulator::Current()->get_register(Simulator::sp);
  mainFP_ = (void*)Simulator::Current()->get_register(Simulator::fp);
  Simulator::Current()->set_register(Simulator::sp, (int)suspendableSP_);
  Simulator::Current()->set_register(Simulator::fp, (int)suspendableFP_);
}
#  endif

#  ifdef JS_SIMULATOR_RISCV64
void SuspenderObjectData::switchSimulatorToMain() {
  suspendableSP_ = (void*)Simulator::Current()->getRegister(Simulator::sp);
  suspendableFP_ = (void*)Simulator::Current()->getRegister(Simulator::fp);
  Simulator::Current()->setRegister(
      Simulator::sp,
      static_cast<int64_t>(reinterpret_cast<uintptr_t>(mainSP_)));
  Simulator::Current()->setRegister(
      Simulator::fp,
      static_cast<int64_t>(reinterpret_cast<uintptr_t>(mainFP_)));
}

void SuspenderObjectData::switchSimulatorToSuspendable() {
  mainSP_ = (void*)Simulator::Current()->getRegister(Simulator::sp);
  mainFP_ = (void*)Simulator::Current()->getRegister(Simulator::fp);
  Simulator::Current()->setRegister(
      Simulator::sp,
      static_cast<int64_t>(reinterpret_cast<uintptr_t>(suspendableSP_)));
  Simulator::Current()->setRegister(
      Simulator::fp,
      static_cast<int64_t>(reinterpret_cast<uintptr_t>(suspendableFP_)));
}
#  endif

// Slots that used in various JSFunctionExtended below.
const size_t SUSPENDER_SLOT = 0;
const size_t WRAPPED_FN_SLOT = 1;
const size_t CONTINUE_ON_SUSPENDABLE_SLOT = 1;
const size_t PROMISE_SLOT = 2;

SuspenderContext::SuspenderContext()
    : activeSuspender_(nullptr), suspendedStacks_() {}

SuspenderContext::~SuspenderContext() {
  MOZ_ASSERT(activeSuspender_ == nullptr);
  MOZ_ASSERT(suspendedStacks_.isEmpty());
}

SuspenderObject* SuspenderContext::activeSuspender() {
  return activeSuspender_;
}

void SuspenderContext::setActiveSuspender(SuspenderObject* obj) {
  activeSuspender_.set(obj);
}

void SuspenderContext::trace(JSTracer* trc) {
  if (activeSuspender_) {
    TraceEdge(trc, &activeSuspender_, "suspender");
  }
}

static JitActivation* FindSuspendableStackActivation(
    JSTracer* trc, const SuspenderObjectData& data) {
  // The jitActivation.refNoCheck() can be used since during trace/marking
  // the main thread will be paused.
  JitActivation* activation =
      trc->runtime()->mainContextFromAnyThread()->jitActivation.refNoCheck();
  while (activation) {
    // Scan all JitActivations to find one that starts with suspended stack
    // frame pointer.
    WasmFrameIter iter(activation);
    if (!iter.done() && data.hasFramePointer(iter.frame())) {
      return activation;
    }
    activation = activation->prevJitActivation();
  }
  MOZ_CRASH("Suspendable stack activation not found");
}

static void TraceSuspendableStack(JSTracer* trc,
                                  const SuspenderObjectData& data) {
  void* exitFP = data.suspendableExitFP();
  MOZ_ASSERT(data.traceable());

  // Create and iterator for wasm frames:
  //  - If a stack entry for suspended stack exists, the data.suspendableFP()
  //    and data.suspendedReturnAddress() provide start of the frames.
  //  - Otherwise, the stack is the part of the main stack, the context
  //    JitActivation frames will be used to trace.
  WasmFrameIter iter =
      data.hasStackEntry()
          ? WasmFrameIter(
                static_cast<FrameWithInstances*>(data.suspendableFP()),
                data.suspendedReturnAddress())
          : WasmFrameIter(FindSuspendableStackActivation(trc, data));
  MOZ_ASSERT_IF(data.hasStackEntry(), iter.currentFrameStackSwitched());
  uintptr_t highestByteVisitedInPrevWasmFrame = 0;
  while (true) {
    MOZ_ASSERT(!iter.done());
    uint8_t* nextPC = iter.resumePCinCurrentFrame();
    Instance* instance = iter.instance();
    TraceInstanceEdge(trc, instance, "WasmFrameIter instance");
    highestByteVisitedInPrevWasmFrame = instance->traceFrame(
        trc, iter, nextPC, highestByteVisitedInPrevWasmFrame);
    if (iter.frame() == exitFP) {
      break;
    }
    ++iter;
    if (iter.currentFrameStackSwitched()) {
      highestByteVisitedInPrevWasmFrame = 0;
    }
  }
}

void SuspenderContext::traceRoots(JSTracer* trc) {
  // The suspendedStacks_ contains suspended stacks frames that need to be
  // traced only during minor GC. The major GC tracing is happening via
  // SuspenderObject::trace.
  // Non-suspended stack frames are traced as part of TraceJitActivations.
  if (!trc->isTenuringTracer()) {
    return;
  }
  gc::AssertRootMarkingPhase(trc);
  for (const SuspenderObjectData& data : suspendedStacks_) {
    TraceSuspendableStack(trc, data);
  }
}

static_assert(JS_STACK_GROWTH_DIRECTION < 0,
              "JS-PI implemented only for native stacks that grows towards 0");

static void DecrementSuspendableStacksCount(JSContext* cx) {
  for (;;) {
    uint32_t currentCount = cx->wasm().suspendableStacksCount;
    MOZ_ASSERT(currentCount > 0);
    if (cx->wasm().suspendableStacksCount.compareExchange(currentCount,
                                                          currentCount - 1)) {
      break;
    }
    // Failed to decrement suspendableStacksCount, repeat.
  }
}

class SuspenderObject : public NativeObject {
 public:
  static const JSClass class_;

  enum { DataSlot, PromisingPromiseSlot, SuspendingReturnTypeSlot, SlotCount };

  enum class ReturnType : int32_t { Unknown, Promise, Exception };

  static SuspenderObject* create(JSContext* cx) {
    for (;;) {
      uint32_t currentCount = cx->wasm().suspendableStacksCount;
      if (currentCount >= SuspendableStacksMaxCount) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_JSPI_SUSPENDER_LIMIT);
        return nullptr;
      }
      if (cx->wasm().suspendableStacksCount.compareExchange(currentCount,
                                                            currentCount + 1)) {
        break;
      }
      // Failed to increment suspendableStacksCount, repeat.
    }

    Rooted<SuspenderObject*> suspender(
        cx, NewBuiltinClassInstance<SuspenderObject>(cx));
    if (!suspender) {
      DecrementSuspendableStacksCount(cx);
      return nullptr;
    }

    void* stackMemory = js_malloc(SuspendableStackPlusRedZoneSize);
    if (!stackMemory) {
      DecrementSuspendableStacksCount(cx);
      ReportOutOfMemory(cx);
      return nullptr;
    }

    SuspenderObjectData* data = js_new<SuspenderObjectData>(stackMemory);
    if (!data) {
      js_free(stackMemory);
      DecrementSuspendableStacksCount(cx);
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_RELEASE_ASSERT(data->state() != SuspenderState::Moribund);

    suspender->initReservedSlot(DataSlot, PrivateValue(data));
    suspender->initReservedSlot(PromisingPromiseSlot, NullValue());
    suspender->initReservedSlot(SuspendingReturnTypeSlot,
                                Int32Value(int32_t(ReturnType::Unknown)));
    return suspender;
  }

  PromiseObject* promisingPromise() const {
    return &getReservedSlot(PromisingPromiseSlot)
                .toObject()
                .as<PromiseObject>();
  }

  void setPromisingPromise(Handle<PromiseObject*> promise) {
    setReservedSlot(PromisingPromiseSlot, ObjectOrNullValue(promise));
  }

  ReturnType suspendingReturnType() const {
    return ReturnType(getReservedSlot(SuspendingReturnTypeSlot).toInt32());
  }

  void setSuspendingReturnType(ReturnType type) {
    // The SuspendingReturnTypeSlot will change after result is defined,
    // and becomes invalid after GetSuspendingPromiseResult. The assert is
    // checking if the result was processed by GetSuspendingPromiseResult.
    MOZ_ASSERT((type == ReturnType::Unknown) !=
               (suspendingReturnType() == ReturnType::Unknown));

    setReservedSlot(SuspendingReturnTypeSlot, Int32Value(int32_t(type)));
  }

  JS::NativeStackLimit getStackMemoryLimit() {
    return JS::NativeStackLimit(data()->stackMemory()) + SuspendableRedZoneSize;
  }

  SuspenderState state() { return data()->state(); }

  inline bool hasData() { return !getReservedSlot(DataSlot).isUndefined(); }

  inline SuspenderObjectData* data() {
    return static_cast<SuspenderObjectData*>(
        getReservedSlot(DataSlot).toPrivate());
  }

  void setMoribund(JSContext* cx);
  void setActive(JSContext* cx);
  void setSuspended(JSContext* cx);

  void enter(JSContext* cx);
  void suspend(JSContext* cx);
  void resume(JSContext* cx);
  void leave(JSContext* cx);

  // Modifies frames to inject the suspendable stack back into the main one.
  void forwardToSuspendable();

 private:
  static const JSClassOps classOps_;

  static void finalize(JS::GCContext* gcx, JSObject* obj);
  static void trace(JSTracer* trc, JSObject* obj);
};

static_assert(SuspenderObjectDataSlot == SuspenderObject::DataSlot);

const JSClass SuspenderObject::class_ = {
    "SuspenderObject",
    JSCLASS_HAS_RESERVED_SLOTS(SlotCount) | JSCLASS_FOREGROUND_FINALIZE,
    &SuspenderObject::classOps_,
};

const JSClassOps SuspenderObject::classOps_ = {
    nullptr,   // addProperty
    nullptr,   // delProperty
    nullptr,   // enumerate
    nullptr,   // newEnumerate
    nullptr,   // resolve
    nullptr,   // mayResolve
    finalize,  // finalize
    nullptr,   // call
    nullptr,   // construct
    trace,     // trace
};

/* static */
void SuspenderObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  SuspenderObject& suspender = obj->as<SuspenderObject>();
  if (!suspender.hasData()) {
    return;
  }
  SuspenderObjectData* data = suspender.data();
  if (data->state() == SuspenderState::Moribund) {
    MOZ_RELEASE_ASSERT(!data->stackMemory());
  } else {
    // Cleaning stack memory and removing from suspendableStacks_.
    data->releaseStackMemory();
    if (SuspenderContext* scx = data->suspendedBy()) {
      scx->suspendedStacks_.remove(data);
    }
  }
  js_free(data);
}

/* static */
void SuspenderObject::trace(JSTracer* trc, JSObject* obj) {
  SuspenderObject& suspender = obj->as<SuspenderObject>();
  if (!suspender.hasData()) {
    return;
  }
  SuspenderObjectData& data = *suspender.data();
  // The SuspenderObjectData refers stacks frames that need to be traced
  // only during major GC to determine if SuspenderObject content is
  // reachable from JS.
  if (!data.traceable() || trc->isTenuringTracer()) {
    return;
  }
  TraceSuspendableStack(trc, data);
}

void SuspenderObject::setMoribund(JSContext* cx) {
  MOZ_ASSERT(state() == SuspenderState::Active);
  ResetInstanceStackLimits(cx);
#  if defined(_WIN32)
  data()->restoreTIBStackFields();
#  endif
  SuspenderObjectData* data = this->data();
  data->setState(SuspenderState::Moribund);
  data->releaseStackMemory();
  DecrementSuspendableStacksCount(cx);
  MOZ_ASSERT(
      !cx->wasm().promiseIntegration.suspendedStacks_.ElementProbablyInList(
          data));
}

void SuspenderObject::setActive(JSContext* cx) {
  data()->setState(SuspenderState::Active);
  UpdateInstanceStackLimitsForSuspendableStack(cx, getStackMemoryLimit());
#  if defined(_WIN32)
  data()->updateTIBStackFields();
#  endif
}

void SuspenderObject::setSuspended(JSContext* cx) {
  data()->setState(SuspenderState::Suspended);
  ResetInstanceStackLimits(cx);
#  if defined(_WIN32)
  data()->restoreTIBStackFields();
#  endif
}

void SuspenderObject::enter(JSContext* cx) {
  MOZ_ASSERT(state() == SuspenderState::Initial);
  cx->wasm().promiseIntegration.setActiveSuspender(this);
  setActive(cx);
#  ifdef DEBUG
  cx->runtime()->jitRuntime()->disallowArbitraryCode();
#  endif
}

void SuspenderObject::suspend(JSContext* cx) {
  MOZ_ASSERT(state() == SuspenderState::Active);
  setSuspended(cx);
  cx->wasm().promiseIntegration.suspendedStacks_.pushFront(data());
  data()->setSuspendedBy(&cx->wasm().promiseIntegration);
  cx->wasm().promiseIntegration.setActiveSuspender(nullptr);
#  ifdef DEBUG
  cx->runtime()->jitRuntime()->clearDisallowArbitraryCode();
#  endif

  if (cx->realm()->isDebuggee()) {
    WasmFrameIter iter(cx->activation()->asJit());
    while (true) {
      MOZ_ASSERT(!iter.done());
      if (iter.debugEnabled()) {
        DebugAPI::onSuspendWasmFrame(cx, iter.debugFrame());
      }
      ++iter;
      if (iter.currentFrameStackSwitched()) {
        break;
      }
    }
  }
}

void SuspenderObject::resume(JSContext* cx) {
  MOZ_ASSERT(state() == SuspenderState::Suspended);
  cx->wasm().promiseIntegration.setActiveSuspender(this);
  setActive(cx);
  data()->setSuspendedBy(nullptr);
  // Use barrier because object is being removed from the suspendable stack
  // from roots.
  gc::PreWriteBarrier(this);
  cx->wasm().promiseIntegration.suspendedStacks_.remove(data());
#  ifdef DEBUG
  cx->runtime()->jitRuntime()->disallowArbitraryCode();
#  endif

  if (cx->realm()->isDebuggee()) {
    for (FrameIter iter(cx);; ++iter) {
      MOZ_RELEASE_ASSERT(!iter.done(), "expecting stackSwitched()");
      if (iter.isWasm()) {
        WasmFrameIter& wasmIter = iter.wasmFrame();
        if (wasmIter.currentFrameStackSwitched()) {
          break;
        }
        if (wasmIter.debugEnabled()) {
          DebugAPI::onResumeWasmFrame(cx, iter);
        }
      }
    }
  }
}

void SuspenderObject::leave(JSContext* cx) {
  cx->wasm().promiseIntegration.setActiveSuspender(nullptr);
#  ifdef DEBUG
  cx->runtime()->jitRuntime()->clearDisallowArbitraryCode();
#  endif
  // We are exiting alternative stack if state is active,
  // otherwise the stack was just suspended.
  switch (state()) {
    case SuspenderState::Active:
      setMoribund(cx);
      break;
    case SuspenderState::Suspended:
      break;
    case SuspenderState::Initial:
    case SuspenderState::Moribund:
      MOZ_CRASH();
  }
}

void SuspenderObject::forwardToSuspendable() {
  // Injecting suspendable stack back into main one at the exit frame.
  SuspenderObjectData* data = this->data();
  uint8_t* mainExitFP = (uint8_t*)data->mainExitFP();
  *reinterpret_cast<void**>(mainExitFP + Frame::callerFPOffset()) =
      data->suspendableFP();
  *reinterpret_cast<void**>(mainExitFP + Frame::returnAddressOffset()) =
      data->suspendedReturnAddress();
}

bool CallOnMainStack(JSContext* cx, CallOnMainStackFn fn, void* data) {
  Rooted<SuspenderObject*> suspender(
      cx, cx->wasm().promiseIntegration.activeSuspender());
  SuspenderObjectData* stacks = suspender->data();

  cx->wasm().promiseIntegration.setActiveSuspender(nullptr);

  MOZ_ASSERT(suspender->state() == SuspenderState::Active);
  suspender->setSuspended(cx);
  // Keep suspendedBy not set -- the stack has no defined entry.
  // See TraceSuspendableStack for details.
  MOZ_RELEASE_ASSERT(suspender->data()->suspendedBy() == nullptr);

#  ifdef JS_SIMULATOR
#    if defined(JS_SIMULATOR_ARM64) || defined(JS_SIMULATOR_ARM) || \
        defined(JS_SIMULATOR_RISCV64)
  // The simulator is using its own stack, however switching is needed for
  // virtual registers.
  stacks->switchSimulatorToMain();
  bool res = fn(data);
  stacks->switchSimulatorToSuspendable();
#    else
#      error "not supported"
#    endif
#  else
  // The platform specific code below inserts offsets as strings into inline
  // assembly. CHECK_OFFSETS verifies the specified literals in macros below.
#    define CHECK_OFFSETS(MAIN_FP_OFFSET, MAIN_SP_OFFSET,               \
                          SUSPENDABLE_FP_OFFSET, SUSPENDABLE_SP_OFFSET) \
      static_assert(                                                    \
          (MAIN_FP_OFFSET) == SuspenderObjectData::offsetOfMainFP() &&  \
          (MAIN_SP_OFFSET) == SuspenderObjectData::offsetOfMainSP() &&  \
          (SUSPENDABLE_FP_OFFSET) ==                                    \
              SuspenderObjectData::offsetOfSuspendableFP() &&           \
          (SUSPENDABLE_SP_OFFSET) ==                                    \
              SuspenderObjectData::offsetOfSuspendableSP());

  // The following assembly code temporarily switches FP/SP pointers to be on
  // main stack, while maintaining frames linking.  After
  // `CallImportData::Call` execution suspendable stack FP/SP will be restored.
  //
  // Because the assembly sequences contain a call, the trashed-register list
  // must contain all the caller saved registers.  They must also contain "cc"
  // and "memory" since both of those state elements could be modified by the
  // call.  They also need a "volatile" qualifier to ensure that the they don't
  // get optimised out or otherwise messed with by clang/gcc.
  //
  // `Registers::VolatileMask` (in the assembler complex) is useful in that it
  // lists the caller-saved registers.

  uintptr_t res;

  // clang-format off
#if defined(_M_ARM64) || defined(__aarch64__)
#    define CALLER_SAVED_REGS \
      "x0", "x1", "x2", "x3","x4", "x5", "x6", "x7", "x8", "x9", "x10",   \
      "x11", "x12", "x13", "x14", "x15", "x16", "x17", /* "x18", */       \
      "x19", "x20", /* it's unclear who saves these two, so be safe */    \
      /* claim that all the vector regs are caller-saved, for safety */   \
      "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10",  \
      "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19",      \
      "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28",      \
      "v29", "v30", "v31"
#    define INLINED_ASM(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP) \
      CHECK_OFFSETS(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP);    \
      asm volatile(                                                       \
          "\n   mov     x0, %1"                                           \
          "\n   mov     x27, sp "                                         \
          "\n   str     x29, [x0, #" #SUSPENDABLE_FP "] "                 \
          "\n   str     x27, [x0, #" #SUSPENDABLE_SP "] "                 \
                                                                          \
          "\n   ldr     x29, [x0, #" #MAIN_FP "] "                        \
          "\n   ldr     x27, [x0, #" #MAIN_SP "] "                        \
          "\n   mov     sp, x27 "                                         \
                                                                          \
          "\n   stp     x0, x27, [sp, #-16]! "                            \
                                                                          \
          "\n   mov     x0, %3"                                           \
          "\n   blr     %2 "                                              \
                                                                          \
          "\n   ldp     x3, x27, [sp], #16 "                              \
                                                                          \
          "\n   mov     x27, sp "                                         \
          "\n   str     x29, [x3, #" #MAIN_FP "] "                        \
          "\n   str     x27, [x3, #" #MAIN_SP "] "                        \
                                                                          \
          "\n   ldr     x29, [x3, #" #SUSPENDABLE_FP "] "                 \
          "\n   ldr     x27, [x3, #" #SUSPENDABLE_SP "] "                 \
          "\n   mov     sp, x27 "                                         \
          "\n   mov     %0, x0"                                           \
          : "=r"(res)                                                     \
          : "r"(stacks), "r"(fn), "r"(data)                               \
          : "x0", "x3", "x27", CALLER_SAVED_REGS, "cc", "memory")
  INLINED_ASM(24, 32, 40, 48);

#  elif defined(_WIN64) && defined(_M_X64)
#    define INLINED_ASM(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP) \
      CHECK_OFFSETS(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP);    \
      asm("\n   mov     %1, %%rcx"                                        \
          "\n   mov     %%rbp, " #SUSPENDABLE_FP "(%%rcx)"                \
          "\n   mov     %%rsp, " #SUSPENDABLE_SP "(%%rcx)"                \
                                                                          \
          "\n   mov     " #MAIN_FP "(%%rcx), %%rbp"                       \
          "\n   mov     " #MAIN_SP "(%%rcx), %%rsp"                       \
                                                                          \
          "\n   push    %%rcx"                                            \
          "\n   push    %%rdx"                                            \
                                                                          \
          "\n   mov     %3, %%rcx"                                        \
          "\n   call    *%2"                                              \
                                                                          \
          "\n   pop    %%rdx"                                             \
          "\n   pop    %%rcx"                                             \
                                                                          \
          "\n   mov     %%rbp, " #MAIN_FP "(%%rcx)"                       \
          "\n   mov     %%rsp, " #MAIN_SP "(%%rcx)"                       \
                                                                          \
          "\n   mov     " #SUSPENDABLE_FP "(%%rcx), %%rbp"                \
          "\n   mov     " #SUSPENDABLE_SP "(%%rcx), %%rsp"                \
                                                                          \
          "\n   movq     %%rax, %0"                                       \
          : "=r"(res)                                                     \
          : "r"(stacks), "r"(fn), "r"(data)                               \
          : "rcx", "rax", "cc", "memory")
  INLINED_ASM(24, 32, 40, 48);

#  elif defined(__x86_64__)
#    define INLINED_ASM(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP) \
      CHECK_OFFSETS(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP);    \
      asm("\n   mov     %1, %%rdi"                                        \
          "\n   mov     %%rbp, " #SUSPENDABLE_FP "(%%rdi)"                \
          "\n   mov     %%rsp, " #SUSPENDABLE_SP "(%%rdi)"                \
                                                                          \
          "\n   mov     " #MAIN_FP "(%%rdi), %%rbp"                       \
          "\n   mov     " #MAIN_SP "(%%rdi), %%rsp"                       \
                                                                          \
          "\n   push    %%rdi"                                            \
          "\n   push    %%rdx"                                            \
                                                                          \
          "\n   mov     %3, %%rdi"                                        \
          "\n   call    *%2"                                              \
                                                                          \
          "\n   pop    %%rdx"                                             \
          "\n   pop    %%rdi"                                             \
                                                                          \
          "\n   mov     %%rbp, " #MAIN_FP "(%%rdi)"                       \
          "\n   mov     %%rsp, " #MAIN_SP "(%%rdi)"                       \
                                                                          \
          "\n   mov     " #SUSPENDABLE_FP "(%%rdi), %%rbp"                \
          "\n   mov     " #SUSPENDABLE_SP "(%%rdi), %%rsp"                \
                                                                          \
          "\n   movq     %%rax, %0"                                       \
          : "=r"(res)                                                     \
          : "r"(stacks), "r"(fn), "r"(data)                               \
          : "rdi", "rax", "cc", "memory")
  INLINED_ASM(24, 32, 40, 48);
#  elif defined(__i386__) || defined(_M_IX86)
#    define CALLER_SAVED_REGS "eax", "ecx", "edx"
#    define INLINED_ASM(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP) \
      CHECK_OFFSETS(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP);    \
      asm("\n   mov     %1, %%edx"                                        \
          "\n   mov     %%ebp, " #SUSPENDABLE_FP "(%%edx)"                \
          "\n   mov     %%esp, " #SUSPENDABLE_SP "(%%edx)"                \
                                                                          \
          "\n   mov     " #MAIN_FP "(%%edx), %%ebp"                       \
          "\n   mov     " #MAIN_SP "(%%edx), %%esp"                       \
                                                                          \
          "\n   push    %%edx"                                            \
          "\n   sub     $8, %%esp"                                        \
          "\n   push    %3"                                               \
          "\n   call    *%2"                                              \
          "\n   add     $12, %%esp"                                       \
          "\n   pop     %%edx"                                            \
                                                                          \
          "\n   mov     %%ebp, " #MAIN_FP "(%%edx)"                       \
          "\n   mov     %%esp, " #MAIN_SP "(%%edx)"                       \
                                                                          \
          "\n   mov     " #SUSPENDABLE_FP "(%%edx), %%ebp"                \
          "\n   mov     " #SUSPENDABLE_SP "(%%edx), %%esp"                \
                                                                          \
          "\n   mov     %%eax, %0"                                        \
          : "=r"(res)                                                     \
          : "r"(stacks), "r"(fn), "r"(data)                               \
          : CALLER_SAVED_REGS, "cc", "memory")
  INLINED_ASM(12, 16, 20, 24);

#  elif defined(__arm__)
#    define INLINED_ASM(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP) \
      CHECK_OFFSETS(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP);    \
      asm("\n   mov     r0, %1"                                           \
          "\n   mov     r1, sp"                                           \
          "\n   str     r11, [r0, #" #SUSPENDABLE_FP "]"                  \
          "\n   str     r1, [r0, #" #SUSPENDABLE_SP "]"                   \
                                                                          \
          "\n   ldr     r11, [r0, #" #MAIN_FP "]"                         \
          "\n   ldr     r1, [r0, #" #MAIN_SP "]"                          \
          "\n   mov     sp, r1"                                           \
                                                                          \
          "\n   str     r0, [sp, #-8]! "                                  \
                                                                          \
          "\n   mov     r0, %3"                                           \
          "\n   blx     %2"                                               \
                                                                          \
          "\n   ldr     r2, [sp], #8 "                                    \
                                                                          \
          "\n   mov     r1, sp"                                           \
          "\n   str     r11, [r2, #" #MAIN_FP "]"                         \
          "\n   str     r1, [r2, #" #MAIN_SP "]"                          \
                                                                          \
          "\n   ldr     r11, [r2, #" #SUSPENDABLE_FP "]"                  \
          "\n   ldr     r1, [r2, #" #SUSPENDABLE_SP "]"                   \
          "\n   mov     sp, r1"                                           \
          "\n   mov     %0, r0"                                           \
          : "=r"(res)                                                     \
          : "r"(stacks), "r"(fn), "r"(data)                               \
          : "r0", "r1", "r2", "r3", "cc", "memory")
  INLINED_ASM(12, 16, 20, 24);

#elif defined(__loongarch_lp64)
#    define CALLER_SAVED_REGS \
      "$ra", "$a0", "$a1", "$a2", "$a3", "$a4", "$a5", "$a6", "$a7",      \
      "$t0", "$t1", "$t2", "$t3", "$t4", "$t5", "$t6", "$t7", "$t8",      \
      "$f0", "$f1", "$f2", "$f3", "$f4", "$f5", "$f6", "$f7", "$f8",      \
      "$f9", "$f10", "$f11", "$f12", "$f13", "$f14", "$f15", "$f16",      \
      "$f17", "$f18", "$f19", "$f20", "$f21", "$f22", "$f23"
#    define INLINED_ASM(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP) \
      CHECK_OFFSETS(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP);    \
      asm volatile(                                                       \
          "\n   move    $a0, %1"                                          \
          "\n   st.d    $fp, $a0, " #SUSPENDABLE_FP                       \
          "\n   st.d    $sp, $a0, " #SUSPENDABLE_SP                       \
                                                                          \
          "\n   ld.d    $fp, $a0, " #MAIN_FP                              \
          "\n   ld.d    $sp, $a0, " #MAIN_SP                              \
                                                                          \
          "\n   addi.d  $sp, $sp, -16"                                    \
          "\n   st.d    $a0, $sp, 8"                                      \
                                                                          \
          "\n   move    $a0, %3"                                          \
          "\n   jirl    $ra, %2, 0"                                       \
                                                                          \
          "\n   ld.d    $a3, $sp, 8"                                      \
          "\n   addi.d  $sp, $sp, 16"                                     \
                                                                          \
          "\n   st.d    $fp, $a3, " #MAIN_FP                              \
          "\n   st.d    $sp, $a3, " #MAIN_SP                              \
                                                                          \
          "\n   ld.d    $fp, $a3, " #SUSPENDABLE_FP                       \
          "\n   ld.d    $sp, $a3, " #SUSPENDABLE_SP                       \
          "\n   move    %0, $a0"                                          \
          : "=r"(res)                                                     \
          : "r"(stacks), "r"(fn), "r"(data)                               \
          : "$a0", "$a3", CALLER_SAVED_REGS, "cc", "memory")
  INLINED_ASM(24, 32, 40, 48);

#  elif defined(__riscv) && defined(__riscv_xlen) && (__riscv_xlen == 64)
#    define CALLER_SAVED_REGS                                             \
        "ra",                                                             \
        "t0", "t1", "t2", "t3", "t4", "t5", "t6",                         \
        "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",                   \
        "ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",           \
        "ft8", "ft9", "ft10", "ft11",                                     \
        "fa0", "fa1", "fa2", "fa3", "fa4", "fa5", "fa6", "fa7",           \
        "fs0", "fs1", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",           \
        "fs8", "fs9", "fs10", "fs11"
#    define INLINED_ASM(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP) \
      CHECK_OFFSETS(MAIN_FP, MAIN_SP, SUSPENDABLE_FP, SUSPENDABLE_SP);    \
      asm volatile(                                                       \
          "\n   mv      a0, %1"                                           \
          "\n   sd      fp, " #SUSPENDABLE_FP "(a0)"                      \
          "\n   sd      sp, " #SUSPENDABLE_SP "(a0)"                      \
                                                                          \
          "\n   ld      fp, " #MAIN_FP "(a0)"                             \
          "\n   ld      sp, " #MAIN_SP "(a0)"                             \
                                                                          \
          "\n   addi    sp, sp, -16"                                      \
          "\n   sd      a0, 8(sp)"                                        \
                                                                          \
          "\n   mv      a0, %3"                                           \
          "\n   jalr    ra, 0(%2)"                                        \
                                                                          \
          "\n   ld      a3, 8(sp)"                                        \
          "\n   addi    sp, sp, 16"                                       \
                                                                          \
          "\n   sd      fp, " #MAIN_FP "(a3)"                             \
          "\n   sd      sp, " #MAIN_SP "(a3)"                             \
                                                                          \
          "\n   ld      fp, " #SUSPENDABLE_FP "(a3)"                      \
          "\n   ld      sp, " #SUSPENDABLE_SP "(a3)"                      \
          "\n   mv      %0, a0"                                           \
          : "=r"(res)                                                     \
          : "r"(stacks), "r"(fn), "r"(data)                               \
          : "ra", "a0", "a3", CALLER_SAVED_REGS, "cc", "memory")
  INLINED_ASM(24, 32, 40, 48);

#  else
  MOZ_CRASH("Not supported for this platform");
#  endif
  // clang-format on
#  endif  // JS_SIMULATOR

  bool ok = (res & 255) != 0;  // need only low byte
  suspender->setActive(cx);
  cx->wasm().promiseIntegration.setActiveSuspender(suspender);

#  undef INLINED_ASM
#  undef CHECK_OFFSETS
#  undef CALLER_SAVED_REGS

  return ok;
}

static void CleanupActiveSuspender(JSContext* cx) {
  SuspenderObject* suspender = cx->wasm().promiseIntegration.activeSuspender();
  MOZ_ASSERT(suspender);
  cx->wasm().promiseIntegration.setActiveSuspender(nullptr);
  suspender->setMoribund(cx);
}

// Suspending

// Builds a wasm module with following structure:
// (module
//   (type $params (struct (field ..)*)))
//   (type $results (struct (field ..)*)))
//   (import "" "" (func $suspending.wrappedfn ..))
//   (func $suspending.exported .. )
//   (func $suspending.trampoline ..)
//   (func $suspending.continue-on-suspendable ..)
//   (export "" (func $suspending.exported))
// )
//
// The module provides logic for the state transitions (see the SMDOC):
//  - Invoke Suspending Import via $suspending.exported
//  - Suspending Function Returns a Promise via $suspending.trampoline
//  - Promise Resolved transitions via $suspending.continue-on-suspendable
//
class SuspendingFunctionModuleFactory {
 public:
  enum TypeIdx {
    ParamsTypeIndex,
    ResultsTypeIndex,
  };

  enum FnIdx {
    WrappedFnIndex,
    ExportedFnIndex,
    TrampolineFnIndex,
    ContinueOnSuspendableFnIndex
  };

 private:
  // Builds function that will be imported to wasm module:
  // (func $suspending.exported
  //   (param ..)* (result ..)*
  //   (local $suspender externref)
  //   (local $results (ref $results))
  //   call $builtin.current-suspender
  //   local.tee $suspender
  //   ref.func $suspending.trampoline
  //   local.get $i*
  //   stuct.new $param-type
  //   stack-switch SwitchToMain ;; <- (suspender,fn,data)
  //   local.get $suspender
  //   call $builtin.get-suspending-promise-result
  //   ref.cast $results-type
  //   local.set $results
  //   (struct.get $results (local.get $results))*
  // )
  bool encodeExportedFunction(CodeMetadata& codeMeta, uint32_t paramsSize,
                              uint32_t resultSize, uint32_t paramsOffset,
                              RefType resultType, Bytes& bytecode) {
    Encoder encoder(bytecode, *codeMeta.types);
    ValTypeVector locals;
    if (!locals.emplaceBack(RefType::extern_())) {
      return false;
    }
    if (!locals.emplaceBack(resultType)) {
      return false;
    }
    if (!EncodeLocalEntries(encoder, locals)) {
      return false;
    }

    const int suspenderIndex = paramsSize;
    if (!encoder.writeOp(Op::I32Const) || !encoder.writeVarU32(0)) {
      return false;
    }
    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32((uint32_t)BuiltinModuleFuncId::CurrentSuspender)) {
      return false;
    }
    if (!encoder.writeOp(Op::LocalTee) ||
        !encoder.writeVarU32(suspenderIndex)) {
      return false;
    }

    // Results local is located after all params and suspender.
    const int resultsIndex = paramsSize + 1;

    if (!encoder.writeOp(Op::RefFunc) ||
        !encoder.writeVarU32(TrampolineFnIndex)) {
      return false;
    }
    for (uint32_t i = 0; i < paramsSize; i++) {
      if (!encoder.writeOp(Op::LocalGet) ||
          !encoder.writeVarU32(i + paramsOffset)) {
        return false;
      }
    }
    if (!encoder.writeOp(GcOp::StructNew) ||
        !encoder.writeVarU32(ParamsTypeIndex)) {
      return false;
    }

    if (!encoder.writeOp(MozOp::StackSwitch) ||
        !encoder.writeVarU32(uint32_t(StackSwitchKind::SwitchToMain))) {
      return false;
    }

    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(suspenderIndex)) {
      return false;
    }
    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32(
            (uint32_t)BuiltinModuleFuncId::GetSuspendingPromiseResult)) {
      return false;
    }
    if (!encoder.writeOp(GcOp::RefCast) ||
        !encoder.writeVarU32(ResultsTypeIndex) ||
        !encoder.writeOp(Op::LocalSet) || !encoder.writeVarU32(resultsIndex)) {
      return false;
    }
    for (uint32_t i = 0; i < resultSize; i++) {
      if (!encoder.writeOp(Op::LocalGet) ||
          !encoder.writeVarU32(resultsIndex) ||
          !encoder.writeOp(GcOp::StructGet) ||
          !encoder.writeVarU32(ResultsTypeIndex) || !encoder.writeVarU32(i)) {
        return false;
      }
    }
    return encoder.writeOp(Op::End);
  }

  // Builds function that is called on main stack:
  // (func $suspending.trampoline
  //   (param $params (ref $suspender)) (param $param (ref $param-type))
  //   (result anyref)
  //   local.get $suspender ;; for $builtin.forward-exn-to-suspended below
  //   block (result exnref)
  //    try_table (catch_all_ref 0)
  //     local.get $suspender ;; for call $add-promise-reactions
  //     (struct.get $param-type $i (local.get $param))*
  //     call $suspending.wrappedfn
  //     ref.func $suspending.continue-on-suspendable
  //     call $builtin.add-promise-reactions
  //     return
  //    end
  //    unreachable
  //   end
  //   call $builtin.forward-exn-to-suspended
  // )
  // The function calls suspending import and returns into the
  // $promising.exported function because that was the top function
  // on the main stack.
  bool encodeTrampolineFunction(CodeMetadata& codeMeta, uint32_t paramsSize,
                                Bytes& bytecode) {
    Encoder encoder(bytecode, *codeMeta.types);
    if (!EncodeLocalEntries(encoder, ValTypeVector())) {
      return false;
    }
    const uint32_t SuspenderIndex = 0;
    const uint32_t ParamsIndex = 1;

    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(SuspenderIndex)) {
      return false;
    }

    if (!encoder.writeOp(Op::Block) ||
        !encoder.writeFixedU8(uint8_t(TypeCode::ExnRef))) {
      return false;
    }

    if (!encoder.writeOp(Op::TryTable) ||
        !encoder.writeFixedU8(uint8_t(TypeCode::BlockVoid)) ||
        !encoder.writeVarU32(1) ||
        !encoder.writeFixedU8(/* catch_all_ref = */ 0x03) ||
        !encoder.writeVarU32(0)) {
      return false;
    }

    // For AddPromiseReactions call below.
    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(SuspenderIndex)) {
      return false;
    }

    for (uint32_t i = 0; i < paramsSize; i++) {
      if (!encoder.writeOp(Op::LocalGet) || !encoder.writeVarU32(ParamsIndex)) {
        return false;
      }
      if (!encoder.writeOp(GcOp::StructGet) ||
          !encoder.writeVarU32(ParamsTypeIndex) || !encoder.writeVarU32(i)) {
        return false;
      }
    }
    if (!encoder.writeOp(Op::Call) || !encoder.writeVarU32(WrappedFnIndex)) {
      return false;
    }
    if (!encoder.writeOp(Op::RefFunc) ||
        !encoder.writeVarU32(ContinueOnSuspendableFnIndex)) {
      return false;
    }

    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32(
            (uint32_t)BuiltinModuleFuncId::AddPromiseReactions)) {
      return false;
    }

    if (!encoder.writeOp(Op::Return) || !encoder.writeOp(Op::End) ||
        !encoder.writeOp(Op::Unreachable) || !encoder.writeOp(Op::End)) {
      return false;
    }

    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32(
            (uint32_t)BuiltinModuleFuncId::ForwardExceptionToSuspended)) {
      return false;
    }

    return encoder.writeOp(Op::End);
  }

  // Builds function that is called on main stack:
  // (func $suspending.continue-on-suspendable
  //   (param $params (ref $suspender)) (param $results externref)
  //   (result externref)
  //   local.get $suspender
  //   ref.null funcref
  //   local.get $results
  //   any.convert_extern
  //   stack-switch ContinueOnSuspendable
  // )
  bool encodeContinueOnSuspendableFunction(CodeMetadata& codeMeta,
                                           uint32_t resultsSize,
                                           Bytes& bytecode) {
    Encoder encoder(bytecode, *codeMeta.types);
    if (!EncodeLocalEntries(encoder, ValTypeVector())) {
      return false;
    }

    const uint32_t SuspenderIndex = 0;
    const uint32_t ResultsIndex = 1;

    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(SuspenderIndex)) {
      return false;
    }
    if (!encoder.writeOp(Op::RefNull) ||
        !encoder.writeValType(ValType(RefType::func()))) {
      return false;
    }
    if (!encoder.writeOp(Op::LocalGet) || !encoder.writeVarU32(ResultsIndex) ||
        !encoder.writeOp(GcOp::AnyConvertExtern)) {
      return false;
    }

    if (!encoder.writeOp(MozOp::StackSwitch) ||
        !encoder.writeVarU32(
            uint32_t(StackSwitchKind::ContinueOnSuspendable))) {
      return false;
    }

    return encoder.writeOp(Op::End);
  }

 public:
  SharedModule build(JSContext* cx, HandleObject func, ValTypeVector&& params,
                     ValTypeVector&& results) {
    FeatureOptions options;
    options.isBuiltinModule = true;

    ScriptedCaller scriptedCaller;
    SharedCompileArgs compileArgs =
        CompileArgs::buildAndReport(cx, std::move(scriptedCaller), options);
    if (!compileArgs) {
      return nullptr;
    }

    MutableModuleMetadata moduleMeta = js_new<ModuleMetadata>();
    if (!moduleMeta || !moduleMeta->init(*compileArgs)) {
      return nullptr;
    }
    MutableCodeMetadata codeMeta = moduleMeta->codeMeta;

    MOZ_ASSERT(IonPlatformSupport());
    CompilerEnvironment compilerEnv(CompileMode::Once, Tier::Optimized,
                                    DebugEnabled::False);
    compilerEnv.computeParameters();

    RefType suspenderType = RefType::extern_();
    RefType promiseType = RefType::extern_();

    ValTypeVector paramsWithoutSuspender;

    const size_t resultsSize = results.length();
    const size_t paramsSize = params.length();
    const size_t paramsOffset = 0;
    if (!paramsWithoutSuspender.append(params.begin(), params.end())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    ValTypeVector resultsRef;
    if (!resultsRef.emplaceBack(promiseType)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }

    StructType boxedParamsStruct;
    if (!StructType::createImmutable(paramsWithoutSuspender,
                                     &boxedParamsStruct)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->types->length() == ParamsTypeIndex);
    if (!codeMeta->types->addType(std::move(boxedParamsStruct))) {
      return nullptr;
    }

    StructType boxedResultType;
    if (!StructType::createImmutable(results, &boxedResultType)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->types->length() == ResultsTypeIndex);
    if (!codeMeta->types->addType(std::move(boxedResultType))) {
      return nullptr;
    }

    MOZ_ASSERT(codeMeta->funcs.length() == WrappedFnIndex);
    if (!moduleMeta->addDefinedFunc(std::move(paramsWithoutSuspender),
                                    std::move(resultsRef))) {
      return nullptr;
    }

    // Imports names are not important, declare functions above as imports.
    codeMeta->numFuncImports = codeMeta->funcs.length();

    // We will be looking up and using the exports function by index so
    // the name doesn't matter.
    MOZ_ASSERT(codeMeta->funcs.length() == ExportedFnIndex);
    if (!moduleMeta->addDefinedFunc(std::move(params), std::move(results),
                                    /*declareForRef = */ true,
                                    mozilla::Some(CacheableName()))) {
      return nullptr;
    }

    ValTypeVector paramsTrampoline, resultsTrampoline;
    if (!paramsTrampoline.emplaceBack(suspenderType) ||
        !paramsTrampoline.emplaceBack(RefType::fromTypeDef(
            &(*codeMeta->types)[ParamsTypeIndex], false)) ||
        !resultsTrampoline.emplaceBack(RefType::any())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->funcs.length() == TrampolineFnIndex);
    if (!moduleMeta->addDefinedFunc(std::move(paramsTrampoline),
                                    std::move(resultsTrampoline),
                                    /*declareForRef = */ true)) {
      return nullptr;
    }

    ValTypeVector paramsContinueOnSuspendable, resultsContinueOnSuspendable;
    if (!paramsContinueOnSuspendable.emplaceBack(suspenderType) ||
        !paramsContinueOnSuspendable.emplaceBack(RefType::extern_())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->funcs.length() == ContinueOnSuspendableFnIndex);
    if (!moduleMeta->addDefinedFunc(std::move(paramsContinueOnSuspendable),
                                    std::move(resultsContinueOnSuspendable),
                                    /*declareForRef = */ true)) {
      return nullptr;
    }

    if (!moduleMeta->prepareForCompile(compilerEnv.mode())) {
      return nullptr;
    }

    ModuleGenerator mg(*codeMeta, compilerEnv, compilerEnv.initialState(),
                       nullptr, nullptr, nullptr);
    if (!mg.initializeCompleteTier()) {
      return nullptr;
    }
    // Build functions and keep bytecodes around until the end.
    uint32_t funcBytecodeOffset = CallSite::FIRST_VALID_BYTECODE_OFFSET;
    Bytes bytecode;
    if (!encodeExportedFunction(
            *codeMeta, paramsSize, resultsSize, paramsOffset,
            RefType::fromTypeDef(&(*codeMeta->types)[ResultsTypeIndex], false),
            bytecode)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    if (!mg.compileFuncDef(ExportedFnIndex, funcBytecodeOffset,
                           bytecode.begin(),
                           bytecode.begin() + bytecode.length())) {
      return nullptr;
    }
    funcBytecodeOffset += bytecode.length();

    Bytes bytecode2;
    if (!encodeTrampolineFunction(*codeMeta, paramsSize, bytecode2)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    if (!mg.compileFuncDef(TrampolineFnIndex, funcBytecodeOffset,
                           bytecode2.begin(),
                           bytecode2.begin() + bytecode2.length())) {
      return nullptr;
    }
    funcBytecodeOffset += bytecode2.length();

    Bytes bytecode3;
    if (!encodeContinueOnSuspendableFunction(*codeMeta, paramsSize,
                                             bytecode3)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    if (!mg.compileFuncDef(ContinueOnSuspendableFnIndex, funcBytecodeOffset,
                           bytecode3.begin(),
                           bytecode3.begin() + bytecode3.length())) {
      return nullptr;
    }
    funcBytecodeOffset += bytecode3.length();

    if (!mg.finishFuncDefs()) {
      return nullptr;
    }

    return mg.finishModule(BytecodeBufferOrSource(), *moduleMeta,
                           /*maybeCompleteTier2Listener=*/nullptr);
  }
};

// Reaction on resolved/rejected suspending promise.
static bool WasmPISuspendTaskContinue(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  // The arg[0] has result of resolved promise, or rejection reason.
  Rooted<JSFunction*> callee(cx, &args.callee().as<JSFunction>());
  RootedValue suspender(cx, callee->getExtendedSlot(SUSPENDER_SLOT));
  RootedValue suspendingPromise(cx, callee->getExtendedSlot(PROMISE_SLOT));

  // Convert result of the promise into the parameters/arguments for the
  // $suspending.continue-on-suspendable.
  RootedFunction continueOnSuspendable(
      cx, &callee->getExtendedSlot(CONTINUE_ON_SUSPENDABLE_SLOT)
               .toObject()
               .as<JSFunction>());
  JS::RootedValueArray<2> argv(cx);
  argv[0].set(suspender);
  argv[1].set(suspendingPromise);

  JS::Rooted<JS::Value> rval(cx);
  if (Call(cx, UndefinedHandleValue, continueOnSuspendable, argv, &rval)) {
    return true;
  }

  // The stack was unwound during exception -- time to release resources.
  CleanupActiveSuspender(cx);

  if (cx->isThrowingOutOfMemory()) {
    return false;
  }
  Rooted<PromiseObject*> promise(
      cx, suspender.toObject().as<SuspenderObject>().promisingPromise());
  return RejectPromiseWithPendingError(cx, promise);
}

// Wraps original import to catch all exceptions and convert result to a
// promise.
// Seen as $suspending.wrappedfn in wasm.
static bool WasmPIWrapSuspendingImport(JSContext* cx, unsigned argc,
                                       Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  Rooted<JSFunction*> callee(cx, &args.callee().as<JSFunction>());
  RootedValue originalImportFunc(cx, callee->getExtendedSlot(WRAPPED_FN_SLOT));

  // Catching exceptions here.
  RootedValue rval(cx);
  if (Call(cx, UndefinedHandleValue, originalImportFunc, args, &rval)) {
    // Convert the result to a resolved promise later in AddPromiseReactions.
    args.rval().set(rval);
    return true;
  }

  // Deferring pending exception to the handler in the
  // $suspending.trampoline.
  return false;
}

JSFunction* WasmSuspendingFunctionCreate(JSContext* cx, HandleObject func,
                                         ValTypeVector&& params,
                                         ValTypeVector&& results) {
  MOZ_ASSERT(IsCallable(ObjectValue(*func)) &&
             !IsCrossCompartmentWrapper(func));

  SuspendingFunctionModuleFactory moduleFactory;
  SharedModule module =
      moduleFactory.build(cx, func, std::move(params), std::move(results));
  if (!module) {
    return nullptr;
  }

  // Instantiate the module.
  Rooted<ImportValues> imports(cx);

  // Add $suspending.wrappedfn to imports.
  RootedFunction funcWrapper(
      cx, NewNativeFunction(cx, WasmPIWrapSuspendingImport, 0, nullptr,
                            gc::AllocKind::FUNCTION_EXTENDED, GenericObject));
  if (!funcWrapper) {
    return nullptr;
  }
  funcWrapper->initExtendedSlot(WRAPPED_FN_SLOT, ObjectValue(*func));
  if (!imports.get().funcs.append(funcWrapper)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  Rooted<WasmInstanceObject*> instance(cx);
  if (!module->instantiate(cx, imports.get(), nullptr, &instance)) {
    // Can also trap on invalid input function.
    return nullptr;
  }

  // Returns the $suspending.exported function.
  RootedFunction wasmFunc(cx);
  if (!WasmInstanceObject::getExportedFunction(
          cx, instance, SuspendingFunctionModuleFactory::ExportedFnIndex,
          &wasmFunc)) {
    return nullptr;
  }
  return wasmFunc;
}

JSFunction* WasmSuspendingFunctionCreate(JSContext* cx, HandleObject func,
                                         const FuncType& type) {
  ValTypeVector params, results;
  if (!params.append(type.args().begin(), type.args().end()) ||
      !results.append(type.results().begin(), type.results().end())) {
    ReportOutOfMemory(cx);
    return nullptr;
  }
  return WasmSuspendingFunctionCreate(cx, func, std::move(params),
                                      std::move(results));
}

// Promising

// Builds a wasm module with following structure:
// (module
//   (type $params (struct (field ..)*))
//   (type $results (struct (field ..)*))
//   (type $create-suspender-result (struct (field externref externref)))
//   (import "" "" (func $promising.wrappedfn ..))
//   (func $promising.exported .. )
//   (func $promising.trampoline ..)
//   (export "" (func $promising.exported))
// )
//
// The module provides logic for the Invoke Promising Import state transition
// via $promising.exported and $promising.trampoline (see the SMDOC).
//
class PromisingFunctionModuleFactory {
 public:
  enum TypeIdx {
    ParamsTypeIndex,
    ResultsTypeIndex,
  };

  enum FnIdx {
    WrappedFnIndex,
    ExportedFnIndex,
    TrampolineFnIndex,
  };

 private:
  // Builds function that will be exported for JS:
  // (func $promising.exported
  //   (param ..)* (result externref)
  //   (local $suspender externref)
  //   call $builtin.create-suspender
  //   local.tee $suspender
  //   call $builtin.create-promising-promise ;; -> (promise)
  //   local.get $suspender
  //   ref.func $promising.trampoline
  //   local.get $i*
  //   stuct.new $param-type
  //   stack-switch SwitchToSuspendable ;; <- (suspender,fn,data)
  // )
  bool encodeExportedFunction(CodeMetadata& codeMeta, uint32_t paramsSize,
                              Bytes& bytecode) {
    Encoder encoder(bytecode, *codeMeta.types);
    ValTypeVector locals;
    if (!locals.emplaceBack(RefType::extern_())) {
      return false;
    }
    if (!EncodeLocalEntries(encoder, locals)) {
      return false;
    }

    const uint32_t SuspenderIndex = paramsSize;
    if (!encoder.writeOp(Op::I32Const) || !encoder.writeVarU32(0)) {
      return false;
    }
    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32((uint32_t)BuiltinModuleFuncId::CreateSuspender)) {
      return false;
    }

    if (!encoder.writeOp(Op::LocalTee) ||
        !encoder.writeVarU32(SuspenderIndex)) {
      return false;
    }
    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32(
            (uint32_t)BuiltinModuleFuncId::CreatePromisingPromise)) {
      return false;
    }

    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(SuspenderIndex)) {
      return false;
    }
    if (!encoder.writeOp(Op::RefFunc) ||
        !encoder.writeVarU32(TrampolineFnIndex)) {
      return false;
    }
    for (uint32_t i = 0; i < paramsSize; i++) {
      if (!encoder.writeOp(Op::LocalGet) || !encoder.writeVarU32(i)) {
        return false;
      }
    }
    if (!encoder.writeOp(GcOp::StructNew) ||
        !encoder.writeVarU32(ParamsTypeIndex)) {
      return false;
    }
    if (!encoder.writeOp(MozOp::StackSwitch) ||
        !encoder.writeVarU32(uint32_t(StackSwitchKind::SwitchToSuspendable))) {
      return false;
    }

    return encoder.writeOp(Op::End);
  }

  // Builds function that is called on alternative stack:
  // (func $promising.trampoline
  //   (param $suspender externref) (param $params (ref $param-type))
  //   (result externref)
  //   local.get $suspender ;; for call $set-results
  //   (local.get $suspender)?
  //   (struct.get $param-type $i (local.get $param))*
  //   (local.get $suspender)?
  //   call $promising.wrappedfn
  //   struct.new $result-type
  //   call $builtin.set-promising-promise-results
  // )
  bool encodeTrampolineFunction(CodeMetadata& codeMeta, uint32_t paramsSize,
                                Bytes& bytecode) {
    Encoder encoder(bytecode, *codeMeta.types);
    if (!EncodeLocalEntries(encoder, ValTypeVector())) {
      return false;
    }
    const uint32_t SuspenderIndex = 0;
    const uint32_t ParamsIndex = 1;

    // Reserved for SetResultsFnIndex call at the end
    if (!encoder.writeOp(Op::LocalGet) ||
        !encoder.writeVarU32(SuspenderIndex)) {
      return false;
    }

    for (uint32_t i = 0; i < paramsSize; i++) {
      if (!encoder.writeOp(Op::LocalGet) || !encoder.writeVarU32(ParamsIndex)) {
        return false;
      }
      if (!encoder.writeOp(GcOp::StructGet) ||
          !encoder.writeVarU32(ParamsTypeIndex) || !encoder.writeVarU32(i)) {
        return false;
      }
    }
    if (!encoder.writeOp(Op::Call) || !encoder.writeVarU32(WrappedFnIndex)) {
      return false;
    }

    if (!encoder.writeOp(GcOp::StructNew) ||
        !encoder.writeVarU32(ResultsTypeIndex)) {
      return false;
    }
    if (!encoder.writeOp(MozOp::CallBuiltinModuleFunc) ||
        !encoder.writeVarU32(
            (uint32_t)BuiltinModuleFuncId::SetPromisingPromiseResults)) {
      return false;
    }

    return encoder.writeOp(Op::End);
  }

 public:
  SharedModule build(JSContext* cx, HandleFunction fn, ValTypeVector&& params,
                     ValTypeVector&& results) {
    const FuncType& fnType = fn->wasmTypeDef()->funcType();
    size_t paramsSize = params.length();

    RefType suspenderType = RefType::extern_();

    FeatureOptions options;
    options.isBuiltinModule = true;

    ScriptedCaller scriptedCaller;
    SharedCompileArgs compileArgs =
        CompileArgs::buildAndReport(cx, std::move(scriptedCaller), options);
    if (!compileArgs) {
      return nullptr;
    }

    MutableModuleMetadata moduleMeta = js_new<ModuleMetadata>();
    if (!moduleMeta || !moduleMeta->init(*compileArgs)) {
      return nullptr;
    }
    MutableCodeMetadata codeMeta = moduleMeta->codeMeta;

    MOZ_ASSERT(IonPlatformSupport());
    CompilerEnvironment compilerEnv(CompileMode::Once, Tier::Optimized,
                                    DebugEnabled::False);
    compilerEnv.computeParameters();

    StructType boxedParamsStruct;
    if (!StructType::createImmutable(params, &boxedParamsStruct)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->types->length() == ParamsTypeIndex);
    if (!codeMeta->types->addType(std::move(boxedParamsStruct))) {
      return nullptr;
    }

    StructType boxedResultType;
    if (!StructType::createImmutable(fnType.results(), &boxedResultType)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->types->length() == ResultsTypeIndex);
    if (!codeMeta->types->addType(std::move(boxedResultType))) {
      return nullptr;
    }

    ValTypeVector paramsForWrapper, resultsForWrapper;
    if (!paramsForWrapper.append(fnType.args().begin(), fnType.args().end()) ||
        !resultsForWrapper.append(fnType.results().begin(),
                                  fnType.results().end())) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->funcs.length() == WrappedFnIndex);
    if (!moduleMeta->addDefinedFunc(std::move(paramsForWrapper),
                                    std::move(resultsForWrapper))) {
      return nullptr;
    }

    // Imports names are not important, declare functions above as imports.
    codeMeta->numFuncImports = codeMeta->funcs.length();

    // We will be looking up and using the exports function by index so
    // the name doesn't matter.
    MOZ_ASSERT(codeMeta->funcs.length() == ExportedFnIndex);
    if (!moduleMeta->addDefinedFunc(std::move(params), std::move(results),
                                    /* declareFoRef = */ true,
                                    mozilla::Some(CacheableName()))) {
      return nullptr;
    }

    ValTypeVector paramsTrampoline, resultsTrampoline;
    if (!paramsTrampoline.emplaceBack(suspenderType) ||
        !paramsTrampoline.emplaceBack(RefType::fromTypeDef(
            &(*codeMeta->types)[ParamsTypeIndex], false))) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    MOZ_ASSERT(codeMeta->funcs.length() == TrampolineFnIndex);
    if (!moduleMeta->addDefinedFunc(std::move(paramsTrampoline),
                                    std::move(resultsTrampoline),
                                    /* declareFoRef = */ true)) {
      return nullptr;
    }

    if (!moduleMeta->prepareForCompile(compilerEnv.mode())) {
      return nullptr;
    }

    ModuleGenerator mg(*codeMeta, compilerEnv, compilerEnv.initialState(),
                       nullptr, nullptr, nullptr);
    if (!mg.initializeCompleteTier()) {
      return nullptr;
    }
    // Build functions and keep bytecodes around until the end.
    Bytes bytecode;
    uint32_t funcBytecodeOffset = CallSite::FIRST_VALID_BYTECODE_OFFSET;
    if (!encodeExportedFunction(*codeMeta, paramsSize, bytecode)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    if (!mg.compileFuncDef(ExportedFnIndex, funcBytecodeOffset,
                           bytecode.begin(),
                           bytecode.begin() + bytecode.length())) {
      return nullptr;
    }
    funcBytecodeOffset += bytecode.length();

    Bytes bytecode2;
    if (!encodeTrampolineFunction(*codeMeta, paramsSize, bytecode2)) {
      ReportOutOfMemory(cx);
      return nullptr;
    }
    if (!mg.compileFuncDef(TrampolineFnIndex, funcBytecodeOffset,
                           bytecode2.begin(),
                           bytecode2.begin() + bytecode2.length())) {
      return nullptr;
    }
    funcBytecodeOffset += bytecode2.length();

    if (!mg.finishFuncDefs()) {
      return nullptr;
    }

    return mg.finishModule(BytecodeBufferOrSource(), *moduleMeta,
                           /*maybeCompleteTier2Listener=*/nullptr);
  }
};

// Wraps call to wasm $promising.exported function to catch an exception and
// return a promise instead.
static bool WasmPIPromisingFunction(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  Rooted<JSFunction*> callee(cx, &args.callee().as<JSFunction>());
  RootedFunction fn(
      cx,
      &callee->getExtendedSlot(WRAPPED_FN_SLOT).toObject().as<JSFunction>());

  // Catching exceptions here.
  if (Call(cx, UndefinedHandleValue, fn, args, args.rval())) {
    return true;
  }

  // During an exception the stack was unwound -- time to release resources.
  CleanupActiveSuspender(cx);

  if (cx->isThrowingOutOfMemory()) {
    return false;
  }

  RootedObject promiseObject(cx, NewPromiseObject(cx, nullptr));
  if (!promiseObject) {
    return false;
  }
  args.rval().setObject(*promiseObject);

  Rooted<PromiseObject*> promise(cx, &promiseObject->as<PromiseObject>());
  return RejectPromiseWithPendingError(cx, promise);
}

JSFunction* WasmPromisingFunctionCreate(JSContext* cx, HandleObject func,
                                        ValTypeVector&& params,
                                        ValTypeVector&& results) {
  RootedFunction wrappedWasmFunc(cx, &func->as<JSFunction>());
  MOZ_ASSERT(wrappedWasmFunc->isWasm());
  const FuncType& wrappedWasmFuncType =
      wrappedWasmFunc->wasmTypeDef()->funcType();

  MOZ_ASSERT(results.length() == 0 && params.length() == 0);
  if (!results.append(RefType::extern_())) {
    ReportOutOfMemory(cx);
    return nullptr;
  }
  if (!params.append(wrappedWasmFuncType.args().begin(),
                     wrappedWasmFuncType.args().end())) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  PromisingFunctionModuleFactory moduleFactory;
  SharedModule module = moduleFactory.build(
      cx, wrappedWasmFunc, std::move(params), std::move(results));
  // Instantiate the module.
  Rooted<ImportValues> imports(cx);

  // Add wrapped function ($promising.wrappedfn) to imports.
  if (!imports.get().funcs.append(func)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  Rooted<WasmInstanceObject*> instance(cx);
  if (!module->instantiate(cx, imports.get(), nullptr, &instance)) {
    MOZ_ASSERT(cx->isThrowingOutOfMemory());
    return nullptr;
  }

  // Wrap $promising.exported function for exceptions/traps handling.
  RootedFunction wasmFunc(cx);
  if (!WasmInstanceObject::getExportedFunction(
          cx, instance, PromisingFunctionModuleFactory::ExportedFnIndex,
          &wasmFunc)) {
    return nullptr;
  }

  RootedFunction wasmFuncWrapper(
      cx, NewNativeFunction(cx, WasmPIPromisingFunction, 0, nullptr,
                            gc::AllocKind::FUNCTION_EXTENDED, GenericObject));
  if (!wasmFuncWrapper) {
    return nullptr;
  }
  wasmFuncWrapper->initExtendedSlot(WRAPPED_FN_SLOT, ObjectValue(*wasmFunc));
  return wasmFuncWrapper;
}

// Gets active suspender.
// The reserved parameter is a workaround for limitation in the
// WasmBuiltinModule.yaml generator to always have params.
// Seen as $builtin.current-suspender to wasm.
SuspenderObject* CurrentSuspender(Instance* instance, int32_t reserved) {
  MOZ_ASSERT(SASigCurrentSuspender.failureMode == FailureMode::FailOnNullPtr);
  JSContext* cx = instance->cx();
  SuspenderObject* suspender = cx->wasm().promiseIntegration.activeSuspender();
  if (!suspender) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_JSPI_INVALID_STATE);
    return nullptr;
  }
  return suspender;
}

// Creates a suspender and promise (that will be returned to JS code).
// Seen as $builtin.create-suspender to wasm.
SuspenderObject* CreateSuspender(Instance* instance, int32_t reserved) {
  MOZ_ASSERT(SASigCreateSuspender.failureMode == FailureMode::FailOnNullPtr);
  JSContext* cx = instance->cx();
  return SuspenderObject::create(cx);
}

// Creates a promise that will be returned at promising call.
// Seen as $builtin.create-promising-promise to wasm.
PromiseObject* CreatePromisingPromise(Instance* instance,
                                      SuspenderObject* suspender) {
  MOZ_ASSERT(SASigCreatePromisingPromise.failureMode ==
             FailureMode::FailOnNullPtr);
  JSContext* cx = instance->cx();

  Rooted<SuspenderObject*> suspenderObject(cx, suspender);
  RootedObject promiseObject(cx, NewPromiseObject(cx, nullptr));
  if (!promiseObject) {
    return nullptr;
  }

  Rooted<PromiseObject*> promise(cx, &promiseObject->as<PromiseObject>());
  suspenderObject->setPromisingPromise(promise);
  return promise.get();
}

// Converts promise results into actual function result, or exception/trap
// if rejected.
// Seen as $builtin.get-suspending-promise-result to wasm.
JSObject* GetSuspendingPromiseResult(Instance* instance, void* result,
                                     SuspenderObject* suspender) {
  MOZ_ASSERT(SASigGetSuspendingPromiseResult.failureMode ==
             FailureMode::FailOnNullPtr);
  JSContext* cx = instance->cx();
  Rooted<SuspenderObject*> suspenderObject(cx, suspender);
  RootedAnyRef resultRef(cx, AnyRef::fromCompiledCode(result));

  SuspenderObject::ReturnType returnType =
      suspenderObject->suspendingReturnType();
  MOZ_ASSERT(returnType != SuspenderObject::ReturnType::Unknown);
  Rooted<PromiseObject*> promise(
      cx, returnType == SuspenderObject::ReturnType::Promise
              ? &resultRef.toJSObject().as<PromiseObject>()
              : nullptr);

#  ifdef DEBUG
  auto resetReturnType = mozilla::MakeScopeExit([&suspenderObject]() {
    suspenderObject->setSuspendingReturnType(
        SuspenderObject::ReturnType::Unknown);
  });
#  endif

  if (promise ? promise->state() == JS::PromiseState::Rejected
              : returnType == SuspenderObject::ReturnType::Exception) {
    // Promise was rejected or an exception was thrown, set pending exception
    // and fail.
    RootedValue reason(
        cx, promise ? promise->reason() : resultRef.get().toJSValue());
    cx->setPendingException(reason, ShouldCaptureStack::Maybe);
    return nullptr;
  }

  // The exception and rejection are handled above -- expect resolved promise.
  MOZ_ASSERT(promise->state() == JS::PromiseState::Fulfilled);
  RootedValue jsValue(cx, promise->value());

  // Construct the results object.
  Rooted<WasmStructObject*> results(
      cx, instance->constantStructNewDefault(
              cx, SuspendingFunctionModuleFactory::ResultsTypeIndex));
  const FieldTypeVector& fields = results->typeDef().structType().fields_;

  if (fields.length() > 0) {
    // The struct object is constructed based on returns of exported function.
    // It is the only way we can get ValType for Val::fromJSValue call.
    const wasm::FuncType& sig = instance->codeMeta().getFuncType(
        SuspendingFunctionModuleFactory::ExportedFnIndex);

    if (fields.length() == 1) {
      RootedVal val(cx);
      MOZ_ASSERT(sig.result(0).storageType() == fields[0].type);
      if (!Val::fromJSValue(cx, sig.result(0), jsValue, &val)) {
        return nullptr;
      }
      results->storeVal(val, 0);
    } else {
      // The multi-value result is wrapped into ArrayObject/Iterable.
      Rooted<ArrayObject*> array(cx);
      if (!IterableToArray(cx, jsValue, &array)) {
        return nullptr;
      }
      if (fields.length() != array->length()) {
        UniqueChars expected(JS_smprintf("%zu", fields.length()));
        UniqueChars got(JS_smprintf("%u", array->length()));
        if (!expected || !got) {
          ReportOutOfMemory(cx);
          return nullptr;
        }

        JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
                                 JSMSG_WASM_WRONG_NUMBER_OF_VALUES,
                                 expected.get(), got.get());
        return nullptr;
      }

      for (size_t i = 0; i < fields.length(); i++) {
        RootedVal val(cx);
        RootedValue v(cx, array->getDenseElement(i));
        MOZ_ASSERT(sig.result(i).storageType() == fields[i].type);
        if (!Val::fromJSValue(cx, sig.result(i), v, &val)) {
          return nullptr;
        }
        results->storeVal(val, i);
      }
    }
  }
  return results;
}

// Collects returned suspending promising, and registers callbacks to
// react on it using WasmPISuspendTaskContinue.
// Seen as $builtin.add-promise-reactions to wasm.
void* AddPromiseReactions(Instance* instance, SuspenderObject* suspender,
                          void* result, JSFunction* continueOnSuspendable) {
  MOZ_ASSERT(SASigAddPromiseReactions.failureMode ==
             FailureMode::FailOnInvalidRef);
  JSContext* cx = instance->cx();

  RootedAnyRef resultRef(cx, AnyRef::fromCompiledCode(result));
  RootedValue resultValue(cx, resultRef.get().toJSValue());
  Rooted<SuspenderObject*> suspenderObject(cx, suspender);
  RootedFunction fn(cx, continueOnSuspendable);

  // Wrap a promise.
  RootedObject promiseConstructor(cx, GetPromiseConstructor(cx));
  RootedObject promiseObj(cx,
                          PromiseResolve(cx, promiseConstructor, resultValue));
  if (!promiseObj) {
    return AnyRef::invalid().forCompiledCode();
  }
  Rooted<PromiseObject*> promiseObject(cx, &promiseObj->as<PromiseObject>());

  suspenderObject->setSuspendingReturnType(
      SuspenderObject::ReturnType::Promise);

  // Add promise reactions
  RootedFunction then_(
      cx, NewNativeFunction(cx, WasmPISuspendTaskContinue, 1, nullptr,
                            gc::AllocKind::FUNCTION_EXTENDED, GenericObject));
  then_->initExtendedSlot(SUSPENDER_SLOT, ObjectValue(*suspenderObject));
  then_->initExtendedSlot(CONTINUE_ON_SUSPENDABLE_SLOT, ObjectValue(*fn));
  then_->initExtendedSlot(PROMISE_SLOT, ObjectValue(*promiseObject));
  if (!JS::AddPromiseReactions(cx, promiseObject, then_, then_)) {
    return AnyRef::invalid().forCompiledCode();
  }
  return AnyRef::fromJSObject(*promiseObject).forCompiledCode();
}

// Changes exit stack frame pointers to suspendable stack and recast exception
// to wasm reference. Seen as $builtin.forward-exn-to-suspended to wasm.
void* ForwardExceptionToSuspended(Instance* instance,
                                  SuspenderObject* suspender, void* exception) {
  MOZ_ASSERT(SASigForwardExceptionToSuspended.failureMode ==
             FailureMode::Infallible);

  suspender->forwardToSuspendable();
  suspender->setSuspendingReturnType(SuspenderObject::ReturnType::Exception);
  return exception;
}

// Resolves the promise using results packed by wasm.
// Seen as $builtin.set-promising-promise-results to wasm.
int32_t SetPromisingPromiseResults(Instance* instance,
                                   SuspenderObject* suspender,
                                   WasmStructObject* results) {
  MOZ_ASSERT(SASigSetPromisingPromiseResults.failureMode ==
             FailureMode::FailOnNegI32);
  JSContext* cx = instance->cx();
  Rooted<WasmStructObject*> res(cx, results);
  Rooted<SuspenderObject*> suspenderObject(cx, suspender);
  RootedObject promise(cx, suspenderObject->promisingPromise());

  const StructType& resultType = res->typeDef().structType();
  RootedValue val(cx);
  // Unbox the result value from the struct, if any.
  switch (resultType.fields_.length()) {
    case 0:
      break;
    case 1: {
      if (!res->getField(cx, /*index=*/0, &val)) {
        return false;
      }
    } break;
    default: {
      Rooted<ArrayObject*> array(cx, NewDenseEmptyArray(cx));
      if (!array) {
        return false;
      }
      for (size_t i = 0; i < resultType.fields_.length(); i++) {
        RootedValue item(cx);
        if (!res->getField(cx, i, &item)) {
          return false;
        }
        if (!NewbornArrayPush(cx, array, item)) {
          return false;
        }
      }
      val.setObject(*array);
    } break;
  }
  ResolvePromise(cx, promise, val);
  return 0;
}

void UpdateSuspenderState(Instance* instance, SuspenderObject* suspender,
                          UpdateSuspenderStateAction action) {
  MOZ_ASSERT(SASigUpdateSuspenderState.failureMode == FailureMode::Infallible);

  JSContext* cx = instance->cx();
  switch (action) {
    case UpdateSuspenderStateAction::Enter:
      suspender->enter(cx);
      break;
    case UpdateSuspenderStateAction::Suspend:
      suspender->suspend(cx);
      break;
    case UpdateSuspenderStateAction::Resume:
      suspender->resume(cx);
      break;
    case UpdateSuspenderStateAction::Leave:
      suspender->leave(cx);
      break;
    default:
      MOZ_CRASH();
  }
}

}  // namespace js::wasm
#endif  // ENABLE_WASM_JSPI
