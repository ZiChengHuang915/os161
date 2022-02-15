#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <mips/trapframe.h>
#include "opt-A1.h"
#include <clock.h>
#define EXITED 1
#define RUNNING 0

  /* this implementation of sys__exit does not do anything with the exit code */
  /* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

  struct addrspace *as;
  struct proc *p = curproc;
  /* for now, just include this to keep the compiler from complaining about
     an unused variable */
  (void)exitcode;

  DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

  KASSERT(curproc->p_addrspace != NULL);
  as_deactivate();
  /*
   * clear p_addrspace before calling as_destroy. Otherwise if
   * as_destroy sleeps (which is quite possible) when we
   * come back we'll be calling as_activate on a
   * half-destroyed address space. This tends to be
   * messily fatal.
   */
  as = curproc_setas(NULL);
  as_destroy(as);

#if OPT_A1
  for (int i = 0; i < (int16_t) array_num(p->p_children); i++) {
    struct proc* temp_child = (struct proc*) array_get(p->p_children, i);
    array_remove(p->p_children, i);

    spinlock_acquire(&temp_child->p_lock);
    if (temp_child->p_exitstatus == EXITED) {
      spinlock_release(&temp_child->p_lock);
      proc_destroy(temp_child);
    } else {
      temp_child->p_parent = NULL;
      spinlock_release(&temp_child->p_lock);
    }
  }
#endif

  /* detach this thread from its process */
  /* note: curproc cannot be used after this call */
  proc_remthread(curthread);

  /* if this is the last user process in the system, proc_destroy()
     will wake up the kernel menu thread */
#if OPT_A1
  spinlock_acquire(&p->p_lock);
  if (p->p_parent->p_exitstatus == RUNNING) {
    p->p_exitstatus = EXITED;
    p->p_exitcode = p->p_exitstatus; //???
    spinlock_release(&p->p_lock);
  } else {
    spinlock_release(&p->p_lock);
    proc_destroy(p);
  }
#else 
  proc_destroy(p); //removed on A1 page 16
#endif

  thread_exit();
  /* thread_exit() does not return, so we should never get here */
  panic("return from thread_exit in sys_exit\n");
}


/* stub handler for getpid() system call                */
int
sys_getpid(pid_t *retval)
{
  /* for now, this is just a stub that always returns a PID of 1 */
  /* you need to fix this to make it work properly */
#if OPT_A1 
  *retval = curproc->p_pid;
#endif
  return(0);
}

/* stub handler for waitpid() system call                */

int
sys_waitpid(pid_t pid,
	    userptr_t status,
	    int options,
	    pid_t *retval)
{
  int exitstatus;
  int result;

  if(curproc->p_pid == pid){
		return ECHILD;
	}
  if(status == NULL) {
    return EFAULT;
  }

  /* this is just a stub implementation that always reports an
     exit status of 0, regardless of the actual exit status of
     the specified process.   
     In fact, this will return 0 even if the specified process
     is still running, and even if it never existed in the first place.

     Fix this!
  */
#if OPT_A1
  bool found = false;
  struct proc* temp_child = NULL;
  for (int i = 0; i < (int16_t) array_num(curproc->p_children); i++) {
    if (((struct proc*) array_get(curproc->p_children, i))->p_pid == pid) {
      found = true;
      temp_child = (struct proc*) array_get(curproc->p_children, i);
      array_remove(curproc->p_children, i);
      break;
    }
  }
  if (!found) {
    return(ESRCH);
  }
  spinlock_acquire (&temp_child->p_lock);
  while (!temp_child->p_exitstatus) {
    spinlock_release (&temp_child->p_lock);
    clocksleep (1);
    spinlock_acquire (&temp_child->p_lock);
  }
  spinlock_release (&temp_child->p_lock);
  //exitstatus = temp_child->p_exitcode;
  exitstatus = _MKWAIT_EXIT(temp_child->p_exitcode);
  proc_destroy(temp_child);
#endif

  if (options != 0) {
    return(EINVAL);
  }
  /* for now, just pretend the exitstatus is 0 */
  //exitstatus = 0;
  result = copyout((void *)&exitstatus,status,sizeof(int));
  if (result) {
    return(result);
  }
  *retval = pid;
  return(0);
}

#if OPT_A1
void thread_fork_temp(void * tf, unsigned long num);
void
thread_fork_temp(void * tf, unsigned long num)
{
  (void)num;

  enter_forked_process((struct trapframe *) tf);

}

int
sys_fork(pid_t* retval, struct trapframe *tf)
{
  unsigned* index_ret;
  int ret;

  struct trapframe* trapframe_for_child = kmalloc(sizeof(struct trapframe));
  if (trapframe_for_child == NULL){
    kprintf("could not create trapframe_for_child\n");
    return ENOMEM;
  }

  struct proc* child = proc_create_runprogram("child");
  if(child == NULL){
    kprintf("could not create child process\n");
    kfree(trapframe_for_child);
    return ENOMEM;
  }

  child->p_parent = curproc;
  array_add(curproc->p_children, child, index_ret);
  ret = as_copy(curproc_getas(), &(child->p_addrspace));
  if (ret) {
    kprintf("could not copy parent address space\n");
    kfree(trapframe_for_child);
    proc_destroy(child);
    return ENOMEM;
  }

  *trapframe_for_child = *tf;

  ret = thread_fork("child_thread", child, thread_fork_temp, trapframe_for_child, 0);
  if (ret) {
    kprintf("could not fork thread\n");
    as_destroy(child->p_addrspace);
    proc_destroy(child);
    kfree(trapframe_for_child);
    return ret;
  }

  *retval = child->p_pid;
  clocksleep(1);
  return 0;
}
#endif

