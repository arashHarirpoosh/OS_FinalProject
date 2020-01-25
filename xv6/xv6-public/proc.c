#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "ticketlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

struct ticketlock tl;
struct ticketlock mutex, write;
//struct spinlock mutex, write;

int nextpid = 1;
int nexttid = 1;
int sharedCounter = 0;
int readerCount=0;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);
static void wakeup1TicketLock(int pid);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

//current proc
// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//current thread
// Disable interrupts so that we are not rescheduled
// while reading thread from the cpu structure
struct thread*
mythread(void) {
    struct cpu *c;
    struct thread *t;
    pushcli();
    c = mycpu();
    t = c->thread;
    popcli();
    return t;
}

static struct thread*
allocthread(struct proc *p) {
    struct thread *t;
    char *sp;

    acquire(p->ttable->lock);
    for (int i = 0; i < MAX_THREADS; i++) {
        t = p->ttable->allthreads[i];
        if (t->tstate == NOTUSED)
            goto found;
    }


    release(p->ttable->lock);
    return 0;

    found:
    t->tstate = EMBRYO;
    t->tid = nexttid++;
    t->tproc = p;

    release(&ptable.lock);

    // Allocate kernel stack.
    if((t->kstack = kalloc()) == 0){
        t->tstate = NOTUSED;
        return 0;
    }
    sp = t->kstack + KSTACKSIZE;

    // Leave room for trap frame.
    sp -= sizeof *t->tf;
    t->tf = (struct trapframe*)sp;

    // Set up new context to start executing at forkret,
    // which returns to trapret.
    sp -= 4;
    *(uint*)sp = (uint)trapret;

    sp -= sizeof *t->context;
    t->context = (struct context*)sp;
    memset(t->context, 0, sizeof *t->context);
    t->context->eip = (uint)forkret;

    return t;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  //char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->ttable->nexttid = 0;
  p->ttable->allthreads[p->ttable->nexttid] = allocthread(p);
  p->ttable->nexttid += 1;

  release(&ptable.lock);

/*  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;*/

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->ttable->allthreads[0]->tf, 0, sizeof(*p->ttable->allthreads[0]->tf));
  p->ttable->allthreads[0]->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->ttable->allthreads[0]->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->ttable->allthreads[0]->tf->es = p->ttable->allthreads[0]->tf->ds;
  p->ttable->allthreads[0]->tf->ss = p->ttable->allthreads[0]->tf->ds;
  p->ttable->allthreads[0]->tf->eflags = FL_IF;
  p->ttable->allthreads[0]->tf->esp = PGSIZE;
  p->ttable->allthreads[0]->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  //p->state = RUNNABLE;
  p->ttable->allthreads[0]->tstate = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(mythread());
  return 0;
}

//Creating a Thread for process
int createThread(void)
{
  (*function, *void) stack;
  int i, pid;
  struct thread *nt;
  struct proc *curproc = myproc();
  struct thread *curthread = mythread();

  // Allocate process.
  if((nt = allocthread()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->ttable->allthreads[0]->kstack);
    np->ttable->allthreads[0]->kstack = 0;
    np->state = UNUSED;
    //np->ttable->allthreads[0]->tstate = NOTUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->ttable->allthreads[0]->tparent = curthread;
  *np->ttable->allthreads[0]->tf = *curthread->tf;
  /////////
  t->context->es = stack
  t->context->eip = function
  /////////

  // Clear %eax so that fork returns 0 in the child.
  np->ttable->allthreads[0]->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();
  struct thread *curthread = mythread();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->ttable->allthreads[0]->kstack);
    np->ttable->allthreads[0]->kstack = 0;
    np->state = UNUSED;
    //np->ttable->allthreads[0]->tstate = NOTUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->ttable->allthreads[0]->tparent = curthread;
  *np->ttable->allthreads[0]->tf = *curthread->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->ttable->allthreads[0]->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  struct thread *t;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc) {
        p->parent = initproc;
        acquire(p->ttable->lock);
        for (int i = 0; i < MAX_THREADS; ++i) {
            t = p->ttable->allthreads[i];
            if (t->tstate == ZOMBIE)
                wakeup1(initproc);
        }
    }
    release(p->ttable->lock);
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  struct thread *t;
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      acquire(p->ttable->lock);
      for (int i=0; i < MAX_THREADS;i++) {
          t = p->ttable->allthreads[i];
          if (t->tstate == ZOMBIE) {
              // Found one.
              pid = p->pid;
              kfree(t->kstack);
              t->kstack = 0;
              freevm(p->pgdir);
              p->pid = 0;
              p->parent = 0;
              p->name[0] = 0;
              p->killed = 0;
              p->state = UNUSED;
              p->ttable->nexttid -= 1;
              t->tid = 0;
              t->tparent = 0;
              t->tstate = NOTUSED;
              release(&ptable.lock);
              return pid;
          }
      }
      release(p->ttable->lock);
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct thread *t;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      /*if(p->state != RUNNABLE)
        continue;*/

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      /*c->proc = p;
      switchuvm(p);*/
      //p->state = RUNNING;

      acquire(p->ttable->lock);
      for(int i = 0; i < MAX_THREADS; i++) {
            t = p->ttable->allthreads[i];
            if(t->tstate != RUNNABLE)
                continue;
          // Switch to chosen process.  It is the process's job
          // to release ptable.lock and then reacquire it
          // before jumping back to us.
            c->proc = p;
            switchuvm(t);

            // Process is done running for now.
            // It should have changed its p->state before coming back.
            //c->proc = 0;
            c->thread = t;
            switchkvm();
            swtch(&(c->scheduler), t->context);
            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = 0;
        }
      release(p->ttable->lock);

    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  //struct proc *p = myproc();
  struct thread *t = mythread();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(t->tstate == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&t->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  //myproc()->state = RUNNABLE;
  mythread()->tstate = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  //struct proc *p = myproc();
  struct thread *t = mythread();
  
  if(t == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  t->chan = chan;
  t->tstate = SLEEPING;

  sched();

  // Tidy up.
  t->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;
  struct thread *t;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      acquire(p->ttable->lock);
      for (int i = 0; i < MAX_THREADS; i++) {
          t = p->ttable->allthreads[i];
          if (t->tstate == SLEEPING && t->chan == chan)
              t->tstate = RUNNABLE;
      }
      release(p->ttable->lock);
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
    acquire(&ptable.lock);
    wakeup1(chan);
    release(&ptable.lock);
}

//PAGEBREAK!
// Wake up process with the specific  pid.
// The ptable lock must be held.
static void
wakeup1TicketLock(int pid)
{
    struct proc *p;
    struct thread *t;

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
        if (p->pid == pid) {
            acquire(p->ttable->lock);
            for (int i = 0; i < MAX_THREADS; i++) {
                t = p->ttable->allthreads[i];
                if (t->tstate == SLEEPING)
                    t->tstate = RUNNABLE;
            }
            release(p->ttable->lock);
        }
    }

}

// Wake up  process with specific pid.
void
wakeupTicketLock(int pid)
{
    acquire(&ptable.lock);
    wakeup1TicketLock(pid);
    release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;
  struct thread *t;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      // Wake threads from sleep if necessary.
      acquire(p->ttable->lock);
      for (int i=0; i < MAX_THREADS; i++) {
          t = p->ttable->allthreads[i];
          if (t->tstate == SLEEPING)
              t->tstate = RUNNABLE;
      }
      release(p->ttable->lock);
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  struct thread *t;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
      acquire(p->ttable->lock);
      for (int j = 0; j < MAX_THREADS; j++) {
          t = p->ttable->allthreads[j];
          if (t->tstate == NOTUSED)
              continue;
          if (t->tstate >= 0 && t->tstate < NELEM(states) && states[t->tstate])
              state = states[t->tstate];
          else
              state = "???";
          cprintf("%d %s %s", p->pid, state, p->name);
          if (t->tstate == SLEEPING) {
              getcallerpcs((uint *) t->context->ebp + 2, pc);
              for (i = 0; i < 10 && pc[i] != 0; i++)
                  cprintf(" %p", pc[i]);
          }
          cprintf("\n");
      }
      release(p->ttable->lock);
  }
}

int
sys_ticketlockInit(void)
{
    sharedCounter = 0;
    initticketlock(&tl,"ticketLock");
    return 0;
}
int
sys_ticketlockTest(void)
{
    acquireticketlock(&tl);
    microdelay(700000);
    sharedCounter++;
    releaseticketlock(&tl);
    return tl.ticket;
}

int
sys_rwinit(void)
{
    sharedCounter = 0;
    initticketlock(&mutex, "mutex readerwriter");
    initticketlock(&write, "write readerwrite");
    /*initlock(&mutex, "m");
    initlock(&write, "w");*/
    return 0;
}

int
sys_rwtest(void)
{
    int pattern;
    int result = 0;
    argint(0, &pattern);
    // Writer
    if (pattern == 1) {
        acquireticketlock(&write);
        microdelay(70);
        sharedCounter++;
        releaseticketlock(&write);
    }
        // Reader
    else if (pattern == 0){
        acquireticketlock(&mutex);
        readerCount++;
        if (readerCount == 1){
            acquireticketlock(&write);
        }
        releaseticketlock(&mutex);
        result = sharedCounter;
        microdelay(70);
        acquireticketlock(&mutex);
        readerCount--;
        if (readerCount == 0){
            releaseticketlock(&write);
        }
        releaseticketlock(&mutex);
    }
    //releaseticketlock(&tl)
    return result;
}

/*int
sys_rwtest(void)
{
    int pattern;
    int result = 0;
    argint(0, &pattern);
    // Writer
    if (pattern == 1) {
        acquire(&write);
        microdelay(1000000);
        sharedCounter++;
        release(&write);
    }
        // Reader
    else if (pattern == 0){
        acquire(&mutex);
        readerCount++;
        if (readerCount == 1){
            acquire(&write);
        }
        release(&mutex);
        result = sharedCounter;
        microdelay(700000);
        acquire(&mutex);
        readerCount--;
        if (readerCount == 0){
            release(&write);
        }
        release(&mutex);
    }
    //releaseticketlock(&tl)
    return result;
}*/
