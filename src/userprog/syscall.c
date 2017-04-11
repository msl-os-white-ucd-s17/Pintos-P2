#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* NEW CHANGE */
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //printf ("system call!\n");

  thread_exit ();
}


/* NEW CHANGE */
/* Terminates Pintos*/
void halt (void) {
	shutdown_power_off();
}

/* Terminated current user program returning status to kernel */
void exit (int status) {

}
