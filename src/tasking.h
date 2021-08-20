//===-- tasking.h - OpenMP task support -------------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef TASKING_H_INCLUDED
#define TASKING_H_INCLUDED

#include <atomic>
#include <cstdint>

namespace lomp {

class Thread;

namespace Tasking {

typedef void (*GnuThunkPointer)(void *);
typedef int32_t (*ThunkPointer)(int32_t, void *);

typedef union {
  int32_t priority;         /**< priority specified by user for the task */
  ThunkPointer destructors; /* pointer to function to invoke
                      deconstructors of firstprivate C++ objects */
  /* future data */
} CompilerData;

struct Taskgroup {
  Taskgroup(Taskgroup * outer_) : outer(outer_), activeTasks(0) {}
  Taskgroup * outer;
  std::atomic<ssize_t> activeTasks;
};

struct TaskDescriptor {
  enum struct Flags {
    Created = 0x0,
    Executing = 0x1,
    Completed = 0x2,
  };
  enum struct ThunkType { LLVMStyle, GNUStyle };
  struct Metadata {
    /* task descriptor for task management */
    Flags flags;
    TaskDescriptor * parent; /* pointer to the parent that created this task */
    Thread * thread; /* pointer to the thread that create the task */
    std::atomic<int>
        childTasks; /* number of child tasks to (potentially) wait for */
    Taskgroup * taskgroup;
  };
  struct Closure {
    /* closure part of the task descriptor */
    void * data; /* pointer to block of pointers to shared vars   */
    ThunkType thunkType;
    union {
      ThunkPointer routine; /* pointer to routine to call for executing task */
      GnuThunkPointer gnuRoutine;
    };
    int32_t partID; /* part id for the task                          */
    CompilerData
        data1; /* Two known optional additions: destructors and priority */
    CompilerData data2; /* Process destructors first, priority second */

    /*  private vars are allocated at the end of the task descriptor */
  };

  Metadata metadata;
  Closure closure;
};

TaskDescriptor::Closure * TaskToClosure(TaskDescriptor * task);
TaskDescriptor * ClosureToTask(TaskDescriptor::Closure * closure);

TaskDescriptor * AllocateTask(size_t sizeOfTaskClosure, size_t sizeOfShareds);
void InitializeTaskDescriptor(TaskDescriptor * task, size_t sizeOfTaskClosure,
                              size_t sizeOfShareds, ThunkPointer task_entry);
void PrepareTask(TaskDescriptor * task);
bool StoreTask(TaskDescriptor * task);
void FreeTask(TaskDescriptor * task);
void FreeTaskAndAncestors(TaskDescriptor * task);
void InvokeTask(TaskDescriptor * task);
void CompleteTask(TaskDescriptor * task);
bool ScheduleTask();
void TaskExecutionBarrier(bool internalBarrier);
bool TaskWait();
void TaskgroupBegin();
void TaskgroupEnd();

} // namespace Tasking

} // namespace lomp

#endif
