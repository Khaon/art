/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ART_RUNTIME_TRACE_H_
#define ART_RUNTIME_TRACE_H_

#include <bitset>
#include <map>
#include <memory>
#include <ostream>
#include <set>
#include <string>
#include <vector>

#include "atomic.h"
#include "base/macros.h"
#include "globals.h"
#include "instrumentation.h"
#include "os.h"
#include "safe_map.h"

namespace art {

namespace mirror {
  class ArtMethod;
  class DexCache;
}  // namespace mirror

class ArtField;
class Thread;

using DexIndexBitSet = std::bitset<65536>;
using ThreadIDBitSet = std::bitset<65536>;

enum TracingMode {
  kTracingInactive,
  kMethodTracingActive,
  kSampleProfilingActive,
};

class Trace FINAL : public instrumentation::InstrumentationListener {
 public:
  enum TraceFlag {
    kTraceCountAllocs = 1,
  };

  enum class TraceOutputMode {
    kFile,
    kDDMS,
    kStreaming
  };

  enum class TraceMode {
    kMethodTracing,
    kSampling
  };

  ~Trace();

  static void SetDefaultClockSource(TraceClockSource clock_source);

  static void Start(const char* trace_filename, int trace_fd, int buffer_size, int flags,
                    TraceOutputMode output_mode, TraceMode trace_mode, int interval_us)
      LOCKS_EXCLUDED(Locks::mutator_lock_,
                     Locks::thread_list_lock_,
                     Locks::thread_suspend_count_lock_,
                     Locks::trace_lock_);
  static void Pause() LOCKS_EXCLUDED(Locks::trace_lock_, Locks::thread_list_lock_);
  static void Resume() LOCKS_EXCLUDED(Locks::trace_lock_);

  // Stop tracing. This will finish the trace and write it to file/send it via DDMS.
  static void Stop()
        LOCKS_EXCLUDED(Locks::mutator_lock_,
                       Locks::thread_list_lock_,
                       Locks::trace_lock_);
  // Abort tracing. This will just stop tracing and *not* write/send the collected data.
  static void Abort()
      LOCKS_EXCLUDED(Locks::mutator_lock_,
                     Locks::thread_list_lock_,
                     Locks::trace_lock_);
  static void Shutdown() LOCKS_EXCLUDED(Locks::trace_lock_);
  static TracingMode GetMethodTracingMode() LOCKS_EXCLUDED(Locks::trace_lock_);

  bool UseWallClock();
  bool UseThreadCpuClock();
  void MeasureClockOverhead();
  uint32_t GetClockOverheadNanoSeconds();

  void CompareAndUpdateStackTrace(Thread* thread, std::vector<mirror::ArtMethod*>* stack_trace)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // InstrumentationListener implementation.
  void MethodEntered(Thread* thread, mirror::Object* this_object,
                     mirror::ArtMethod* method, uint32_t dex_pc)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE;
  void MethodExited(Thread* thread, mirror::Object* this_object,
                    mirror::ArtMethod* method, uint32_t dex_pc,
                    const JValue& return_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE;
  void MethodUnwind(Thread* thread, mirror::Object* this_object,
                    mirror::ArtMethod* method, uint32_t dex_pc)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE;
  void DexPcMoved(Thread* thread, mirror::Object* this_object,
                  mirror::ArtMethod* method, uint32_t new_dex_pc)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE;
  void FieldRead(Thread* thread, mirror::Object* this_object,
                 mirror::ArtMethod* method, uint32_t dex_pc, ArtField* field)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE;
  void FieldWritten(Thread* thread, mirror::Object* this_object,
                    mirror::ArtMethod* method, uint32_t dex_pc, ArtField* field,
                    const JValue& field_value)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE;
  void ExceptionCaught(Thread* thread, mirror::Throwable* exception_object)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE;
  void BackwardBranch(Thread* thread, mirror::ArtMethod* method, int32_t dex_pc_offset)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) OVERRIDE;
  // Reuse an old stack trace if it exists, otherwise allocate a new one.
  static std::vector<mirror::ArtMethod*>* AllocStackTrace();
  // Clear and store an old stack trace for later use.
  static void FreeStackTrace(std::vector<mirror::ArtMethod*>* stack_trace);
  // Save id and name of a thread before it exits.
  static void StoreExitingThreadInfo(Thread* thread);

  static TraceOutputMode GetOutputMode() LOCKS_EXCLUDED(Locks::trace_lock_);
  static TraceMode GetMode() LOCKS_EXCLUDED(Locks::trace_lock_);

 private:
  Trace(File* trace_file, const char* trace_name, int buffer_size, int flags,
        TraceOutputMode output_mode, TraceMode trace_mode);

  // The sampling interval in microseconds is passed as an argument.
  static void* RunSamplingThread(void* arg) LOCKS_EXCLUDED(Locks::trace_lock_);

  static void StopTracing(bool finish_tracing, bool flush_file);
  void FinishTracing() SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  void ReadClocks(Thread* thread, uint32_t* thread_clock_diff, uint32_t* wall_clock_diff);

  void LogMethodTraceEvent(Thread* thread, mirror::ArtMethod* method,
                           instrumentation::Instrumentation::InstrumentationEvent event,
                           uint32_t thread_clock_diff, uint32_t wall_clock_diff)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);

  // Methods to output traced methods and threads.
  void GetVisitedMethods(size_t end_offset, std::set<mirror::ArtMethod*>* visited_methods);
  void DumpMethodList(std::ostream& os, const std::set<mirror::ArtMethod*>& visited_methods)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_);
  void DumpThreadList(std::ostream& os) LOCKS_EXCLUDED(Locks::thread_list_lock_);

  // Methods to register seen entitites in streaming mode. The methods return true if the entity
  // is newly discovered.
  bool RegisterMethod(mirror::ArtMethod* method)
      SHARED_LOCKS_REQUIRED(Locks::mutator_lock_) EXCLUSIVE_LOCKS_REQUIRED(streaming_lock_);
  bool RegisterThread(Thread* thread)
      EXCLUSIVE_LOCKS_REQUIRED(streaming_lock_);

  // Copy a temporary buffer to the main buffer. Used for streaming. Exposed here for lock
  // annotation.
  void WriteToBuf(const uint8_t* src, size_t src_size)
      EXCLUSIVE_LOCKS_REQUIRED(streaming_lock_);

  // Singleton instance of the Trace or null when no method tracing is active.
  static Trace* volatile the_trace_ GUARDED_BY(Locks::trace_lock_);

  // The default profiler clock source.
  static TraceClockSource default_clock_source_;

  // Sampling thread, non-zero when sampling.
  static pthread_t sampling_pthread_;

  // Used to remember an unused stack trace to avoid re-allocation during sampling.
  static std::unique_ptr<std::vector<mirror::ArtMethod*>> temp_stack_trace_;

  // File to write trace data out to, null if direct to ddms.
  std::unique_ptr<File> trace_file_;

  // Buffer to store trace data.
  std::unique_ptr<uint8_t> buf_;

  // Flags enabling extra tracing of things such as alloc counts.
  const int flags_;

  // The kind of output for this tracing.
  const TraceOutputMode trace_output_mode_;

  // The tracing method.
  const TraceMode trace_mode_;

  const TraceClockSource clock_source_;

  // Size of buf_.
  const int buffer_size_;

  // Time trace was created.
  const uint64_t start_time_;

  // Clock overhead.
  const uint32_t clock_overhead_ns_;

  // Offset into buf_.
  AtomicInteger cur_offset_;

  // Did we overflow the buffer recording traces?
  bool overflow_;

  // Map of thread ids and names that have already exited.
  SafeMap<pid_t, std::string> exited_threads_;

  // Sampling profiler sampling interval.
  int interval_us_;

  // Streaming mode data.
  std::string streaming_file_name_;
  Mutex* streaming_lock_;
  std::map<mirror::DexCache*, DexIndexBitSet*> seen_methods_;
  std::unique_ptr<ThreadIDBitSet> seen_threads_;

  DISALLOW_COPY_AND_ASSIGN(Trace);
};

}  // namespace art

#endif  // ART_RUNTIME_TRACE_H_
