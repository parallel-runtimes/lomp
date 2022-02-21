//===-- tasking.cc - OpenMP task support ------------------------*- C++ -*-===//
//
// Part of the LOMP project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <cstring>
#include <ctime>
#include <cstdio>
#include <cassert>

#include <algorithm>
#include <array>
#include <deque>
#include <mutex>
#include <random>

#include "debug.h"

#include "tasking.h"
#include "threads.h"
#include "numa_support.h"

// Configuration options for the tasking support of LOMP
#define USE_LINKED_LIST_LIFO_TASK_POOL 0
#define USE_RESTR_LINKED_LIST_LIFO_TASK_POOL 0
#define USE_ARRAY_TASK_POOL 0
#define USE_DEQUE_TASK_POOL 1

#define TASK_POOL_MAX_SIZE 128

#define USE_SINGLE_TASK_POOL 0
#define USE_MULTI_TASK_POOL 1

#define USE_RANDOM_STEALING 0
#if (LOMP_TARGET_LINUX)
// This task stealing algorithm requires Linux support thread affinity.
#define USE_NUMA_AWARE_RANDOM_STEALING 1
#else
// Fallback implementation if we're not on a Linux system.
#define USE_ROUND_ROBIN_STEALING 1
#endif

#define DEBUG_TASKING 0

namespace lomp::Tasking {

template <class Lock, size_t maxSize>
struct TaskPoolDeque {
  TaskPoolDeque() : taskCount(0), pool() {}

  bool put(TaskDescriptor * task) {
    const auto guard = std::lock_guard{lock};
    if (taskCount >= maxSize) {
#if DEBUG_TASKING
      printf("put: task pool full, task=%p, #tasks=%ld, max size deque: %ld\n",
             task, taskCount, pool.size());
#endif
      return false;
    }

    pool.push_back(task);
    taskCount++;

#if DEBUG_TASKING
    printf("put: task=%p, #tasks=%ld, max size deque: %ld\n", task, taskCount,
           pool.size());
#endif
    return true;
  }

  TaskDescriptor * get() {
    TaskDescriptor * task = nullptr;
    const auto guard = std::lock_guard{lock};
    if (taskCount > 0) {
      task = pool.back();
      pool.pop_back();
      taskCount--;
    }
#if DEBUG_TASKING
    printf("get: task=%p, #tasks=%ld\n", task, taskCount);
#endif
    return task;
  }

  TaskDescriptor * steal() {
    TaskDescriptor * task = nullptr;
    const auto guard = std::lock_guard{lock};
    if (taskCount > 0) {
      task = pool.front();
      pool.pop_front();
      taskCount--;
    }
#if DEBUG_TASKING
    printf("steal: task=%p, #tasks=%ld\n", task, taskCount);
#endif
    return task;
  }

private:
  Lock lock;
  size_t taskCount;
  std::deque<TaskDescriptor *> pool;
};

template <class Lock, size_t maxSize>
struct TaskPoolArrayLIFO {
  TaskPoolArrayLIFO() : taskCount(0) {
    pool.fill(nullptr);
  }

  bool put(TaskDescriptor * task) {
    const auto guard = std::lock_guard{lock};
    if (taskCount >= maxSize) {
#if DEBUG_TASKING
      printf("put: task pool full, task=%p, #tasks=%ld\n", task, taskCount);
#endif
      return false;
    }
    pool[taskCount] = task;
    taskCount++;

#if DEBUG_TASKING
    printf("put: task=%p, #tasks=%ld\n", task, taskCount);
#endif
    return true;
  }

  TaskDescriptor * get() {
    TaskDescriptor * task = nullptr;
    const auto guard = std::lock_guard{lock};
    if (taskCount > 0) {
      task = pool[taskCount - 1];
      pool[taskCount - 1] = nullptr;
      taskCount--;
    }
#if DEBUG_TASKING
    printf("get: task=%p, #tasks=%ld\n", task, taskCount);
#endif
    return task;
  }

#if USE_MULTI_TASK_POOL
  TaskDescriptor * steal() {
    // As we only have a single task pool for all threads, steal() and
    // get() are equivalent.
    return get();
  }
#endif

private:
  size_t taskCount;
  std::array<TaskDescriptor *, maxSize> pool;
  Lock lock;
};

template <class Lock>
struct TaskPoolLinkedListLIFO {
  TaskPoolLinkedListLIFO() : head(nullptr), taskCount(0) {}

  bool put(TaskDescriptor * task) {
    auto * node = new ListNode{nullptr, task};
    if (!node) {
      return false;
    }
    {
      const auto guard = std::lock_guard{lock};
      node->next = head;
      head = node;
      taskCount++;
#if DEBUG_TASKING
      printf("put: thread=%p, task=%p, #tasks=%ld, head=%p, head->next=%p\n",
             Thread::getCurrentThread(), task, taskCount, head,
             head ? head->next : nullptr);
#endif
    }
    return true;
  }

  TaskDescriptor * get() {
    ListNode * node = nullptr;
    TaskDescriptor * task = nullptr;
    const auto guard = std::lock_guard{lock};
    if (head) {
      node = head;
      head = head->next;
    }
    if (node) {
      task = node->task;
      delete node;
      taskCount--;
    }
    return task;
  }

#if USE_MULTI_TASK_POOL
  TaskDescriptor * steal() {
    // As we only have a single task pool for all threads, steal() and
    // get() are equivalent.
    return get();
  }
#endif

private:
  struct ListNode {
    ListNode * next;
    TaskDescriptor * task;
  };
  ListNode * head;
  size_t taskCount;
  Lock lock;
};

template <class Lock, size_t maxSize>
struct TaskPoolRestrictedLinkedListLIFO {
  TaskPoolRestrictedLinkedListLIFO() {
    head = nullptr;
    taskCount = 0;
  }

  bool put(TaskDescriptor * task) {
    auto * node = new ListNode;
    std::lock_guard<Lock> lock_guard(lock);
    node->task = task;
    node->next = nullptr;
    if (!node) {
      return false;
    }
    if (taskCount == maxSize) {
      return false;
    }
    if (head) {
      node->next = head;
      head = node;
    }
    else {
      head = node;
    }
    taskCount++;
#if DEBUG_TASKING
    printf("put: thread=%p, task=%p, #tasks=%ld, head=%p, head->next=%p\n",
           Thread::getCurrentThread(), task, taskCount, head,
           head ? head->next : nullptr);
#endif
    return true;
  }

  TaskDescriptor * get() {
    ListNode * node = nullptr;
    TaskDescriptor * task = nullptr;
    std::lock_guard<Lock> lock_guard(lock);
    if (head) {
      node = head;
      head = head->next;
    }
    if (node) {
      task = node->task;
      delete node;
      taskCount--;
    }
    return task;
  }

#if USE_MULTI_TASK_POOL
  TaskDescriptor * steal() {
    // As we only have a single task pool for all threads, steal() and
    // get() are equivalent.
    return get();
  }
#endif

private:
  struct ListNode {
    ListNode * next;
    TaskDescriptor * task;
  };
  ListNode * head;
  size_t taskCount;
  Lock lock;
};

#if USE_LINKED_LIST_LIFO_TASK_POOL
typedef TaskPoolLinkedListLIFO<std::mutex> TaskPoolImpl;
#endif
#if USE_RESTR_LINKED_LIST_LIFO_TASK_POOL
typedef TaskPoolRestrictedLinkedListLIFO<std::mutex, TASK_POOL_MAX_SIZE>
    TaskPoolImpl;
#endif
#if USE_ARRAY_TASK_POOL
typedef TaskPoolArrayLIFO<std::mutex, TASK_POOL_MAX_SIZE> TaskPoolImpl;
#endif
#if USE_DEQUE_TASK_POOL
typedef TaskPoolDeque<std::mutex, TASK_POOL_MAX_SIZE> TaskPoolImpl;
#endif

struct TaskPool : private TaskPoolImpl {
  using TaskPoolImpl::get;
  using TaskPoolImpl::put;
#if USE_MULTI_TASK_POOL
  using TaskPoolImpl::steal;
#endif
};

TaskPool * TaskPoolFactory() {
  TaskPool * pool;
#if USE_SINGLE_TASK_POOL
  static TaskPool taskPool{};
  pool = &taskPool;
#endif
#if USE_MULTI_TASK_POOL
  pool = new TaskPool{};
#endif
#if DEBUG_TASKING
  printf("task pool factory, task pool %p\n", pool);
#endif
  return pool;
}

TaskDescriptor::Closure * TaskToClosure(TaskDescriptor * task) {
  // Return a pointer to the closure contained in this task object.
  return &task->closure;
}

TaskDescriptor * ClosureToTask(TaskDescriptor::Closure * closure) {
  // Determine the offset at which the closure object is embedded within the
  // task object.
  TaskDescriptor * dummy = nullptr;
  size_t offset = ((char *)&dummy->closure) - ((char *)&dummy->metadata);

  // Subtract offset from closure pointer and return reconstructed task
  // pointer.
  dummy = (TaskDescriptor *)(((char *)closure) - offset);
  return dummy;
}

size_t ComputeAllocSize(size_t sizeOfTaskClosure, size_t sizeOfShareds) {
  auto allocDiff = sizeof(TaskDescriptor) - sizeof(TaskDescriptor::Closure);
  return std::max(sizeof(TaskDescriptor::Closure), sizeOfTaskClosure) +
         sizeOfShareds + allocDiff;
}

TaskDescriptor * AllocateTask(size_t sizeOfTaskClosure, size_t sizeOfShareds) {
  auto allocSize = ComputeAllocSize(sizeOfTaskClosure, sizeOfShareds);
  auto task = static_cast<TaskDescriptor *>(malloc(allocSize));
  if (!task) {
    // TODO: handle this error
  }
#if DEBUG_TASKING
  memset(static_cast<void *>(task), 0, allocSize);
  printf("create task: sizeof(Task)=%ld, sizeOfTaskClosure=%ld, "
         "sizeOfShareds=%ld, task=%p\n",
         sizeof(TaskDescriptor), sizeOfTaskClosure, sizeOfShareds, task);
#endif
  return task;
}

void InitializeTaskDescriptor(TaskDescriptor * task, size_t sizeOfTaskClosure,
                              size_t sizeOfShareds, ThunkPointer task_entry) {
  auto * thread = Thread::getCurrentThread();
  auto * taskgroup = thread->getCurrentTaskgroup();

  // if task-private variables have been allocated, record the location
  // of the task-private space in the task descriptor
  if (sizeOfShareds > 0) {
    size_t offset = sizeof(task->metadata) + sizeOfTaskClosure;
    task->closure.data = ((char *)task) + offset;
#if DEBUG_TASKING
    printf("offset = %ld, relative=%ld\n",
           ((char *)task->closure.data) - ((char *)task),
           ((char *)task->closure.data) - ((char *)&task->closure));
#endif
  }
  else {
    task->closure.data = nullptr;
  }
  task->closure.routine = task_entry;

  // Set the default calling convention for thunk functions to LLVM's.
  task->closure.thunkType = TaskDescriptor::ThunkType::LLVMStyle;

  // initialize the the child-parent relationship
  task->metadata.flags = TaskDescriptor::Flags::Created;
  task->metadata.childTasks.store(0);
  task->metadata.thread = thread;
  task->metadata.parent = thread->getCurrentTask();
  task->metadata.taskgroup = taskgroup;

#if DEBUG_TASKING
  printf("init task: thread=%p, task=%p, closure=%p, task->data=%p, "
         "task->parent=%p, task->taskgroup=%p\n",
         Thread::getCurrentThread(), task, &task->closure, task->closure.data,
         task->metadata.parent, task->metadata.taskgroup);
#endif
}

void PrepareTask(TaskDescriptor * task) {
  auto * thread = Thread::getCurrentThread();
  auto * team = thread->getTeam();

  // Count this task as being created for determining how many tasks are left to
  // be executed.
  assert(team->activeTasks.load() >= 0);
  ++team->activeTasks;

  // Record that a new child has been created and increment the parent's child
  // counter (or in the thread descriptor if the task is coming from an implicit
  // task).
  if (task->metadata.parent) {
    assert(task->metadata.parent->metadata.childTasks.load() >= 0);
    task->metadata.parent->metadata.childTasks++;
  }
  else {
    assert(task->metadata.thread->childTasks.load() >= 0);
    task->metadata.thread->childTasks++;
  }
 
  // Now we have to also record this task as active for a potentially active
  // taskgroup
  if (auto * taskgroup = task->metadata.taskgroup; taskgroup) {
    assert(taskgroup->activeTasks.load() >= 0);
    taskgroup->activeTasks++;
  }
}

bool StoreTask(TaskDescriptor * task) {
  auto * taskPool = Thread::getCurrentThread()->getTaskPool();
  
  // Try to put the task into the pool.
  if (taskPool->put(task)) {
    return true;
  } else {
    // There was no free slot in the task pool. Execute the task immediately,
    // to avoid a stall of execution.
    InvokeTask(task);
    return false;
  }
}

void FreeTask(TaskDescriptor * task) {
  // memset(task, 0, sizeof(TaskDescriptor));
  free(task);
}

void FreeTaskAndAncestors(TaskDescriptor * task) {
  // This lock prevents freeing tasks while another thread is also attempting to
  // purge completed tasks.
  static std::mutex lock;
  const auto guard = std::lock_guard{lock};
  size_t children = 0;

#if DEBUG_TASKING
  auto * thread = Thread::getCurrentThread();
  int tid = thread->getLocalId();
#endif

  // Determine how many children of the task are still registered in the system
  // as executing.
  children = task->metadata.childTasks;
  while (!children) {
    // The task does not have any active children and is flagged as completed,
    // so it can be deallocated and we can proceed to the parent task in the
    // ancestor chain.
    if (task->metadata.flags == TaskDescriptor::Flags::Completed) {
      TaskDescriptor * purge = task;
      task = task->metadata.parent;
#if DEBUG_TASKING
      printf("FreeTaskAndAncestors: freeing task %p in thread %d\n", purge,
             tid);
#endif
      FreeTask(purge);
    }

    // If a (ancestor) task is reached that is still actively executing, we
    // abort walking the ancestor chain, as anything above (incl this task)
    // cannot be deallocated.
    if (!task || task->metadata.flags != TaskDescriptor::Flags::Completed)
      break;
    children = task->metadata.childTasks;
  }
}

void InvokeTask(TaskDescriptor * task) {
  auto * thread = Thread::getCurrentThread();
  auto * team = thread->getTeam();
  int32_t gtid = 0;

  // store the reference to the previously running task
  TaskDescriptor * previous = thread->getCurrentTask();

#if DEBUG_TASKING
  printf("executing task: thread=%p, prev task=%p, new task=%p, "
         "task->data=%p\n",
         thread, previous, task, task->closure.data);
#endif

  // Set the thread's current task pointer to the to-be-executed
  // task and execute the task's code.
  thread->setCurrentTask(task);
  task->metadata.flags = TaskDescriptor::Flags::Executing;

  // Determine the calling conventions for the thunk function and use the proper
  // function pointer and way of passing in the task's data area.
  switch (task->closure.thunkType) {
  case TaskDescriptor::ThunkType::LLVMStyle:
    task->closure.routine(gtid, &task->closure);
    break;
#if (LOMP_GNU_SUPPORT)
  case TaskDescriptor::ThunkType::GNUStyle:
    task->closure.gnuRoutine(task->closure.data);
    break;
#endif
  default:
    fatalError("Unknown thunk calling style %d.", task->closure.thunkType);
  }

  // Mark the task as completed and do all the book keeping
  CompleteTask(task);

  // Free the task descriptor and garbage-collect (grand)parent tasks that are
  // still pointing to this child task.
  if (task->metadata.childTasks == 0) {
    FreeTaskAndAncestors(task);
  }

  // Restore the previous reference to the previously executing task.
  thread->setCurrentTask(previous);
}

void CompleteTask(TaskDescriptor * task) {
  auto * thread = Thread::getCurrentThread();
  auto * team = thread->getTeam();

  task->metadata.flags = TaskDescriptor::Flags::Completed;

  // When this task finished, the parent task now has one child task left
  // to wait for (if the parent task is waiting)
  if (task->metadata.parent) {
#if DEBUG_TASKING
    printf("dec cntr: parent=%p\n", task->metadata.parent);
#endif
    task->metadata.parent->metadata.childTasks--;
    assert(task->metadata.parent->metadata.childTasks.load() >= 0);
  }
  else {
    task->metadata.thread->childTasks--;
    assert(task->metadata.thread->childTasks.load() >= 0);
  }

  // Now we have to also record this task as being no longer active for a
  // potentially active taskgroup
  if (auto * taskgroup = task->metadata.taskgroup; taskgroup) {
    --taskgroup->activeTasks;
    assert(taskgroup->activeTasks.load() >= 0);
  }

  // And, finally, record the task as being done for the parallel region
  --team->activeTasks;
  assert(team->activeTasks.load() >= 0);
}

#if USE_MULTI_TASK_POOL
#if USE_ROUND_ROBIN_STEALING
struct RoundRobinStealTask {
  TaskDescriptor * operator()() {
    auto * thread = Thread::getCurrentThread();
    auto * team = thread->getTeam();
    auto me = thread->getLocalId();
    auto teamSize = team->getCount();
    TaskDescriptor * task = nullptr;
    for (size_t i = 0; i < teamSize; ++i) {
      // Start looking for a task in task pool of the next door neighbour.
      auto victim = team->getThread((me + i) % teamSize);
      auto victimPool = victim->getTaskPool();
      task = victimPool->steal();
      if (!task) {
        continue;
      }
      else {
        break;
      }
    }
    return task;
  }
};
#endif

#if USE_RANDOM_STEALING
struct RandomStealTask {
  TaskDescriptor * operator()() {
    auto * thread = Thread::getCurrentThread();
    auto * team = thread->getTeam();
    auto me = thread->getLocalId();
    auto teamSize = team->getCount();
    TaskDescriptor * task = nullptr;

    // Pick a random victim task pool and return it.
    auto rnd = thread->nextRandom();
    auto victimID = (me + rnd) % teamSize;
    auto * victimPool = team->getThread(victimID)->getTaskPool();
    task = victimPool->steal();
    return task;
  }
};
#endif

#if USE_NUMA_AWARE_RANDOM_STEALING
struct NumaStealStask {
  TaskDescriptor * operator()() {
    auto * thread = Thread::getCurrentThread();
    TaskDescriptor * task = nullptr;

    // Determine NUMA domain of the thief
    auto numberOfDomains = numa::GetNumberOfNumaDomains();
    auto myCore = numa::GetCoreForThread(thread);
    if (myCore == -1) {
      // for some reason we could not determine the core that this thread
      // was execution on.
      lomp::printWarning(
          "NUMA database did return a proper core ID for thread %p", thread);
      return nullptr;
    }
    auto myDomain = numa::GetNumaDomain(myCore);

    // The algorithm starts with the current NUMA domain and goes round robin
    // until a task has been stolen or we reach the last NUMA domain.
    for (size_t domain = 0; domain < numberOfDomains && !task; ++domain) {
      // Determine the victim domain, its array of core IDs, and the number of
      // cores in this domain.  We have to do this for each domain, as each
      // domain can have a different number of cores.
      auto victimDomain = (myDomain + domain) % numberOfDomains;
      auto coresDomain = numa::GetCoresForNumaDomain(victimDomain);
      auto numberOfCoresDomain = coresDomain.size();

      // Go through all the cores of this NUMA domain and check if a task has
      // been stolen.  We do not check the current core, as it has no task in
      // the task pool (it's trying to steal, as it ran our of tasks!).
      for (size_t victimCore = 0; victimCore < numberOfCoresDomain && !task;
           ++victimCore) {
        // Translate domain-local core ID into global core ID
        auto globalCoreID = coresDomain.at(victimCore);

        // Skip thief
        if (myCore == globalCoreID) {
          continue;
        }

        // Determine the thread has is registered to run on this core.
        auto victimThread = numa::GetThreadForCore(globalCoreID);
        if (victimThread) {
          auto victimPool = victimThread->getTaskPool();
          task = victimPool->steal();
        }
      }
    }

    return task;
  }
};
#endif

#if USE_ROUND_ROBIN_STEALING
typedef RoundRobinStealTask StealTaskImpl;
#endif
#if USE_RANDOM_STEALING
typedef RandomStealTask StealTaskImpl;
#endif
#if USE_NUMA_AWARE_RANDOM_STEALING
typedef NumaStealStask StealTaskImpl;
#endif
#endif

struct StealTask : private StealTaskImpl {
  using StealTaskImpl::operator();
};

bool ScheduleTask() {
  auto * thread = Thread::getCurrentThread();
  auto * taskPool = thread->getTaskPool();
  bool result = false;

  // Try to retrieve a task from the task pool.
  TaskDescriptor * task = taskPool->get();

#if USE_MULTI_TASK_POOL
  if (!task) {
    // No task available in the local pool, so steal from another threads task pool.
    task = StealTask{}();
  }
#endif

  // If a task was retrieved from the task pool, invoke its closure and execute
  // the task.
  if (task) {
#if DEBUG_TASKING
    printf("invoking task from pool: task=%p\n", task);
#endif
    InvokeTask(task);
    result = true;
  }
  return result;
}

void TaskExecutionBarrier(bool internalBarrier) {
  auto * thread = Thread::getCurrentThread();
  auto * team = thread->getTeam();
  size_t teamSize = team->getCount();
  size_t goal = internalBarrier ? teamSize : 0;
#if DEBUG_TASKING
  printf("thread %d: barrier, start task execution (goal %ld)\n",
         thread->getLocalId(), goal);
#endif
  while (team->activeTasks.load() != goal) {
    while (ScheduleTask())
      ;
  }
#if DEBUG_TASKING
  printf("thread %d: barrier, done task execution\n", thread->getLocalId());
#endif
}

bool TaskWait() {
  // Determine the task that executes the encountered taskwait: it is the
  // currently scheduled task on the current threads.
  auto * thread = Thread::getCurrentThread();
  TaskDescriptor * parent = thread->getCurrentTask();

  if (parent) {
#if DEBUG_TASKING
    printf("taskwait/explicit: thread=%p, task=%p, childTasks=%d\n", thread,
           parent, parent->metadata.childTasks.load());
#endif

    // Wait at this point until a direct descendant tasks of this parent task
    // have completed.
    while (parent->metadata.childTasks) {
      // The current task has to wait, and so we try to find a different task
      // to execute to not waste cycles by just spin waiting.
      ScheduleTask();
#if DEBUG_TASKING
      printf("taskwait/explicit: thread=%p, task=%p, childTasks=%d\n", thread,
             parent, parent->metadata.childTasks.load());
#endif
    }
  }
  else {
#if DEBUG_TASKING
    printf("taskwait/implicit: thread=%p, childTasks=%ld\n", thread,
           thread->childTasks.load());
#endif

    // Wait at this point until a direct descendant tasks of this parent task
    // have completed.
    while (thread->childTasks) {
      // The current task has to wait, and so we try to find a different task
      // to execute to not waste cycles by just spin waiting.
      ScheduleTask();
#if DEBUG_TASKING
      printf("taskwait/implicit: thread=%p, childTasks=%ld\n", thread,
             thread->childTasks.load());
#endif
    }
  }
  return true;
}

void TaskgroupBegin() {
  auto * thread = Thread::getCurrentThread();
  auto * outer = thread->getCurrentTaskgroup();

  auto * inner = new Taskgroup(outer);
  thread->setCurrentTaskgroup(inner);

#if DEBUG_TASKING
  auto id = thread->getLocalId();
  printf("thread %d, taskgroup=%p\n", id, inner);
#endif
}

void TaskgroupEnd() {
  auto * thread = Thread::getCurrentThread();
  auto * taskgroup = thread->getCurrentTaskgroup();

#if DEBUG_TASKING
  auto id = thread->getLocalId();
  printf("thread %d, taskgroup %p: enter wait for %ld child tasks\n", id,
         taskgroup, taskgroup->activeTasks.load());
#endif
  // When this call happens, we can be sure that a task group is active, or the
  // compiler did something really wrong.
  if (taskgroup) {
    while (taskgroup->activeTasks) {
      // The current task has to wait, and so we try to find a different task
      // to execute to not waste cycles by just spin waiting.
      ScheduleTask();

      // TODO: do we want to use the same pattern here as in the Taskbarrier() i
      // implementation:
      // while (ScheduleTask())
      //   ;
    }

    auto * outer = taskgroup->outer;
    thread->setCurrentTaskgroup(outer);
  }
}

} // namespace lomp::Tasking
