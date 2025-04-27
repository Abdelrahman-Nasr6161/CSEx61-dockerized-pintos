#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/fixed-point.h"
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "fixed_point.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Number of timer ticks per second. */
#define TIMER_FREQ 100  //////////

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

/* MLFQS parameters */
#define F (1 << 14)             /* Fixed-point scale factor */
#define NICE_DEFAULT 0          /* Default nice value */
#define RECENT_CPU_DEFAULT 0    /* Default recent_cpu value */
#define LOAD_AVG_DEFAULT 0      /* Default load_avg value */

static int load_avg;        /* System load average */

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

/* MLFQS helper functions */
static void mlfqs_update_load_avg(void);
static void mlfqs_update_recent_cpu(void);
static void mlfqs_update_priority(struct thread *t);
static void mlfqs_increment_recent_cpu(void);
static void mlfqs_recalc_priorities(void);

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();

  /* Initialize MLFQS variables */
  load_avg = FP_INT_TO_FP(LOAD_AVG_DEFAULT);
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
   void
   thread_tick (void) 
   {
     struct thread *t = thread_current ();
   
     /* Update statistics. */
     if (t == idle_thread)
       idle_ticks++;
   #ifdef USERPROG
     else if (t->pagedir != NULL)
       user_ticks++;
   #endif
     else
       kernel_ticks++;
   
     /* Update MLFQS statistics if enabled */
     if (thread_mlfqs) {
       /* Increment recent_cpu for running thread */
       if (t != idle_thread)
         t->recent_cpu = FP_ADD_MIX(t->recent_cpu, 1);
   
       /* Every second (TIMER_FREQ ticks), update load_avg and recent_cpu */
       if (timer_ticks () % TIMER_FREQ == 0) {
         mlfqs_update_load_avg ();
         mlfqs_update_all_recent_cpu ();
       }
   
       /* Update priority every tick */
       mlfqs_update_thread_priority(t);
     }
   
     /* Enforce preemption. */
     if (++thread_ticks >= TIME_SLICE)
       intr_yield_on_return ();
   }

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Initialize MLFQS parameters */
  t->nice = NICE_DEFAULT;
  t->recent_cpu = RECENT_CPU_DEFAULT;
  if (thread_mlfqs) {
    mlfqs_update_priority(t);
  }

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  /* Add to run queue. */
  thread_unblock (t);
  
  /* If MLFQS is enabled, we don't yield based on priority */
  if (!thread_mlfqs && priority > thread_get_priority())
    thread_yield();

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock(). */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  
  if (thread_mlfqs) {
    /* For MLFQS, we need to calculate the priority before inserting */
    mlfqs_update_thread_priority(t);
    list_insert_ordered(&ready_list, &t->elem, thread_priority_comparator, NULL);
  } else {
    /* For priority scheduling with donations, just insert based on current priority */
    list_insert_ordered(&ready_list, &t->elem, thread_priority_comparator, NULL);
  }
  
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) {
    if (thread_mlfqs) {
      mlfqs_update_thread_priority(cur);
      list_insert_ordered(&ready_list, &cur->elem, thread_priority_comparator, NULL);
    } else {
      list_push_back (&ready_list, &cur->elem);
    }
  }
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  if (thread_mlfqs) 
    return;  /* Priority is controlled by scheduler in MLFQS mode */

  struct thread *curr = thread_current();
  curr->initial_priority = new_priority;

  if (!thread_mlfqs) {
    refresh_priority();

  /* Check if we need to yield to a higher priority thread */
  if (!list_empty(&ready_list)) {
    struct list_elem *e = list_max(&ready_list, thread_priority_comparator, NULL);
    struct thread *max_ready = list_entry(e, struct thread, elem);
    if (max_ready->priority > curr->priority)
      thread_yield();
  }
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  if (thread_mlfqs) {
    return thread_current()->priority;
  } else {
    struct thread *current = thread_current();
    if (list_empty(&current->donations))
      return current->priority;
    
    struct list_elem *e;
    int max_priority = current->priority;
    for (e = list_begin(&current->donations); e != list_end(&current->donations); e = list_next(e)) {
      struct thread *donor = list_entry(e, struct thread, donation_elem);
      if (donor->priority > max_priority) {
        max_priority = donor->priority;
      }
    }
    return max_priority;
  }
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice) 
{
  ASSERT (nice >= -20 && nice <= 20);
  
  struct thread *t = thread_current();
  t->nice = nice;
  // printf("Thread %s set nice to %d\n", t->name, nice);
  
  if (thread_mlfqs) {
    mlfqs_update_thread_priority(t);
    thread_yield();
  }
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  return thread_current()->nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  return FP_TO_INT_NEAREST(FP_MULT_MIX(load_avg, 100));
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  return FP_TO_INT_NEAREST(FP_MULT_MIX(thread_current()->recent_cpu, 100));
}

/* Update a thread's priority for MLFQS */
void
mlfqs_update_thread_priority(struct thread *t) 
{
    if (t == idle_thread)
        return;

    /* priority = PRI_MAX - (recent_cpu / 4) - (nice * 2) */
    int new_priority = FP_TO_INT_ROUND(  // Use round instead of nearest for better accuracy
        FP_SUB(
            FP_SUB(
                FP_INT_TO_FP(PRI_MAX),
                FP_DIV_MIX(t->recent_cpu, 4)
            ),
            FP_INT_TO_FP(t->nice * 2)
        )
    );
    
    /* Clamp priority between PRI_MIN and PRI_MAX */
    t->priority = new_priority < PRI_MIN ? PRI_MIN :
                  new_priority > PRI_MAX ? PRI_MAX : new_priority;
}

/* Update priorities of all threads */
void
mlfqs_update_all_priorities(void) 
{
  mlfqs_update_all_recent_cpu();
  struct list_elem *e;
  
  for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
    struct thread *t = list_entry(e, struct thread, allelem);
    mlfqs_update_load_avg();
    mlfqs_update_thread_priority(t);
  }
}

/* Update recent_cpu for all threads */
void
mlfqs_update_all_recent_cpu(void) 
{
    struct list_elem *e;
    
    /* Calculate coefficient: (2 * load_avg) / (2 * load_avg + 1) */
    fixed_t two_load_avg = FP_MULT_MIX(load_avg, 2);
    fixed_t coefficient = FP_DIV(two_load_avg, FP_ADD_MIX(two_load_avg, 1));
 
    for (e = list_begin(&all_list); e != list_end(&all_list); e = list_next(e)) {
        struct thread *t = list_entry(e, struct thread, allelem);
        if (t != idle_thread) {
            /* recent_cpu = coefficient * recent_cpu + nice */
            t->recent_cpu = FP_ADD(
                FP_MULT(coefficient, t->recent_cpu), 
                FP_INT_TO_FP(t->nice)
            );
        }
    }
}

/* Update the system load average */
void
mlfqs_update_load_avg(void) 
{
    /* ready_threads includes running thread (if not idle) */
    int ready_threads = list_size(&ready_list) + (thread_current() != idle_thread ? 1 : 0);
    
    /* load_avg = (59/60)*load_avg + (1/60)*ready_threads */
    load_avg = FP_ADD(
        FP_MULT(FP_DIV(FP_INT_TO_FP(59), FP_INT_TO_FP(60)), load_avg),
        FP_DIV(FP_INT_TO_FP(ready_threads), FP_INT_TO_FP(60))
    );
}
/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      intr_disable ();
      thread_block ();
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);
  intr_enable ();
  function (aux);
  thread_exit ();
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
   static void
init_thread (struct thread *t, const char *name, int priority)
{
    enum intr_level old_level;

    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);

    memset (t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->stack = (uint8_t *) t + PGSIZE;

    /* Initialize priority and donation list */
    t->initial_priority = priority;
    t->priority = priority;
    list_init (&t->donations);
    list_init (&t->locks);
    t->waiting_lock = NULL;

    /* Initialize MLFQS-related fields */
    t->nice = NICE_DEFAULT;
    t->recent_cpu = FP_INT_TO_FP(RECENT_CPU_DEFAULT);  // Convert to fixed-point
    
    t->magic = THREAD_MAGIC;
    
    /* Add to all_list safely */
    old_level = intr_disable ();
    list_push_back (&all_list, &t->allelem);
    intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void)
{
  if (list_empty(&ready_list))
    return idle_thread;

  if (thread_mlfqs) {
    /* For MLFQS, the ready list is maintained in priority order */
    return list_entry(list_pop_front(&ready_list), struct thread, elem);
  } else {
    /* For priority scheduling, find the thread with highest priority */
    struct list_elem *max_elem = list_max(&ready_list, thread_priority_comparator, NULL);
    list_remove(max_elem);
    return list_entry(max_elem, struct thread, elem);
  }
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  cur->status = THREAD_RUNNING;
  thread_ticks = 0;

#ifdef USERPROG
  process_activate ();
#endif

  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

/* Comparator to check which thread has the highest priority */
bool 
thread_priority_comparator(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) 
{
  struct thread *thread_a = list_entry(a, struct thread, elem);
  struct thread *thread_b = list_entry(b, struct thread, elem);
  return thread_a->priority > thread_b->priority;
}

/* Refresh the priority of the current thread considering donations */
// void
// refresh_priority(void) 
// {
//   if (thread_mlfqs) 
//     return;

//   struct thread *t = thread_current();
//   int old_priority = t->priority;
  
//   /* Priority is the max of initial priority and all donations */
//   int new_priority = t->initial_priority;
//   if (!list_empty(&t->donations)) {
//     struct list_elem *e = list_max(&t->donations, thread_priority_comparator, NULL);
//     struct thread *donor = list_entry(e, struct thread, donation_elem);
//     if (donor->priority > new_priority)
//       new_priority = donor->priority;
//   }
  
//   t->priority = new_priority;
  
//   /* If priority increased, we might need to yield */
//   if (new_priority > old_priority && !list_empty(&ready_list)) {
//     struct list_elem *e = list_max(&ready_list, thread_priority_comparator, NULL);
//     struct thread *max_ready = list_entry(e, struct thread, elem);
//     if (max_ready->priority > new_priority)
//       thread_yield();
//   }
// }