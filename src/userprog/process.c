#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

static thread_func start_process NO_RETURN;
static bool load (user_program *p_user_prog, void (**eip) (void), void **esp);

/* ADDED BY STEFANI MOORE */
struct list lists;	/* List of all lists */

static struct child_exec {
	bool succes;			/*  */
	char* cmd_line; 		/*  */
	struct child_parent *child_ptr;	/*  */
	semaphore child_loaded;		/*  */
};

/***********************************************************************************************************************
MODIFIED BY SHAWN JOHNSON AND LENA BANKS AND STEFANI MOORE
Starts a new thread running a user program loaded from FILENAME.  The new thread may be scheduled (and may even exit)
before process_execute() returns.  Returns the new process's thread id, or TID_ERROR if the thread cannot be created.

ADDED BY STEFANI : Memory is allocated for parsed file_name, string file_name is copied to fn_name, a thread is created using the parsed fn_name, the memory previously allocated for fn_name is later freed, and the child thread lock is downed. If the thread is not executed successfully the function will return with a value of -1.

Using fn_name to maintain the integrity of the file_name string
***********************************************************************************************************************/
tid_t
process_execute (const char *file_name) 
{
  char *fn_copy = NULL;
  tid_t tid;
  char *save_ptr = NULL;
  char *fn_name = NULL;


  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);
  fn_name = (char *) malloc(strlen(file_name) + 1);
  strlcpy(fn_name, file_name, strlen(file_name)+1);

  char *arg = strtok_r(fn_name, " ", &save_ptr);
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (arg, PRI_DEFAULT, start_process, fn_copy);
  free (fn_name);

  if (tid == TID_ERROR)
    palloc_free_page (fn_copy); 

	/* ADDED BY STEFANI MOORE */
	sema_down (&thread_current ()->child_sema);
	if(!thread_current()->success)
		return -1;


	return tid;
}

/***********************************************************************************************************************
MODIFIED BY SHAWN JOHNSON AND LENA BANKS AND STEFANI MOORE

A thread function that loads a user process and starts it running.

If the prcoess does not start successfully must set bool success of thread struct to false, else it did start successfully and must set it to true. In both cases must sema UP the child_sema lock of the parent thread
***********************************************************************************************************************/
static void
start_process (void *file_name_)
{
  //**********************************************************************************************************************
//MODIFIED BY SHAWN JOHNSON AND LENA BANKS
  char *throwaway;
  char *arg;
  char *cpy;
  char *save_ptr;
  char *file_name = file_name_;
  int arg_index = 0;

  user_program user_prog;
  user_program *pup = &user_prog;

  /* extracts file name and arguments using strtok_r; first get file name, then up to 16 arguments separated by spaces*/
  file_name = strtok_r(file_name_, " ", &throwaway);
  cpy = (char *) malloc(strlen(arg)+1);
  strlcpy(cpy, arg, strlen(arg)+1);
  user_prog.file_name = cpy;
  arg = strtok_r(cpy, " ", &save_ptr);
  while(arg != NULL && arg_index < 16){
    cpy = (char *) malloc(strlen(arg)+1);
    strlcpy(cpy, arg, strlen(arg)+1);
    user_prog.args[arg_index] = cpy;
    arg = strtok_r(NULL, " ", &save_ptr);
    arg_index++;
  }


  user_prog.arg_count = arg_index;

  bool success;
  struct intr_frame if_;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (pup, &if_.eip, &if_.esp);

 	/* If load failed, quit. */
  palloc_free_page (file_name);
  if (!success) { 
    thread_current ()->parent_thread->success = false;
		sema_up (&thread_current ()->parent_thread->child_sema);
		thread_exit ();
	}
	else {
		thread_current ()->parent_thread->success = true;
		sema_up (&thread_current ()->parent_thread->child_sema);
	}


	// Parsing occures in execution
  
  //palloc_get_page:  palloc flag 004 indicates user page
//**********************************************************************************************************************
 

  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
NOT_REACHED ();
}

/***********************************************************************************************************************
MODIFIED BY STEFANI MOORE

Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.
***********************************************************************************************************************/
int
process_wait (tid_t child_tid) 
{
	/* ADDED BY STEFANI MOORE */
  struct list_elem *e;
	struct child_kernel_thread * _c = NULL;
	struct list_elem *_e = NULL;

	for (e = list_begin (&thread_current ()->child_processes); e != list_end (&thread_current ()->child_processes); e = list_next (e))
	{
			struct child_kernel_thread * c = list_entry (e, struct child_kernel_thread, elem);
			if (c->tid == child_tid)
			{
					_c = c;
					_e = e;
			}
	}

	if (!_c || !_e)
		return -1;

	thread_current ()-> waiting_on_thread = _c->tid;

	if (!_c->was_used)
		sema_down (&thread_current ()->child_sema);

	int status = _c->exit_status;
	list_remove(_e);

	return status;
}

/***********************************************************************************************************************
MODIFIED BY STEFANI MOORE

Free the current process's resources.
***********************************************************************************************************************/
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

	/* ADDED BY STEFANI MOORE */
	if(cur->exit_status == -100)
			proc_exit(-1);

	int exit_status = cur->exit_status;

	
	//TODO : Implement closing of all files

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (user_program *p_user_prog, void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);
/* ADDED BY STEFANI MOORE */
void proc_exit (int status);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (user_program *p_user_prog, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  file = filesys_open ((*p_user_prog).file_name);


  if (file == NULL) 
    {
      printf ("load: %s: open failed\n", (*p_user_prog).file_name);
      goto done; 
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", (*p_user_prog).file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (p_user_prog, esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;
  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  file_close (file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      /* Get a page of memory. */
      uint8_t *kpage = palloc_get_page (PAL_USER);
      if (kpage == NULL)
        return false;

      /* Load this page. */
      if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
        {
          palloc_free_page (kpage);
          return false; 
        }
      memset (kpage + page_read_bytes, 0, page_zero_bytes);

      /* Add the page to the process's address space. */
      if (!install_page (upage, kpage, writable)) 
        {
          palloc_free_page (kpage);
          return false; 
        }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
    }
  return true;
}
 /***********************************************************************************************************************
MODIFIED BY SHAWN JOHNSON AND LENA BANKS

Create a minimal stack by mapping a zeroed page at the top of user virtual memory.
***********************************************************************************************************************/
static bool
setup_stack (user_program *user_prog, void **esp) 
{
   uint8_t *kpage;
  bool success = false;

  kpage = palloc_get_page (PAL_USER | PAL_ZERO);
  if (kpage != NULL) 
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success)
        *esp = PHYS_BASE;
      else {
        palloc_free_page(kpage);
        return success;
      }
//**********************************************************************************************************************
//MODIFIED BY SHAWN JOHNSON AND LENA BANKS AND STEFANI MOORE

      int num_bytes = 0;
      char *throwaway = NULL;
      char *arg = NULL;


      // Extracts file name and arguments using strtok_r; first get file name,
      // then up to 16 arguments separated by spaces
	
      void *save_ptr = *esp;

      //printf("Push strings\n");
      for (int i = (*user_prog).arg_count - 1; i >= 0; --i) {
        size_t len = strlen((*user_prog).args[i]) + 1;
        *esp -= len;
        strlcpy(*esp, (*user_prog).args[i], len);
        //printf("Reference to argv[%d] : 0x%llx \n", i + 1, (unsigned long long) *vp);
        num_bytes += (int) len;
      }

     *esp -= strlen((*user_prog).file_name) + 1;
     strlcpy(*esp, (*user_prog).file_name, strlen((*user_prog).file_name) + 1);
      //printf("Reference to argv[0] : 0x%llx \n", (unsigned long long) *vp);


    num_bytes += strlen((*user_prog).file_name) + 1;
    int align = num_bytes % 4;

      if (align > 0) {
        //printf("Word align\n");
        for (unsigned int i = 0; i < align; ++i) {
          *esp -= sizeof(NULL_BYTE);
          memset(*esp, NULL_BYTE, sizeof(NULL_BYTE));
        }
      }

      //printf("Push null\n");
      *esp -= sizeof(char *);
      memset(*esp, 0, sizeof(char *));

      //printf("Push string pointers\n");
      for (int i = (*user_prog).arg_count - 1; i >= 0; --i) {
        *esp -= sizeof(char *);
        save_ptr -= strlen((*user_prog).args[i]) + 1;
        memcpy(*esp, &save_ptr, sizeof(char *));
        //printf("Pointer to argv[%d]: %llx : %llx \n", i + 1, (unsigned long long) *vp, (unsigned long long) save_ptr);
      }

      *esp -= sizeof(char *);
      save_ptr -= strlen((*user_prog).file_name) + 1;
      memcpy(*esp, &save_ptr, sizeof(char *));
      //printf("Pointer to argv[0]: 0x%llx : 0x%llx \n", (unsigned int) *vp, (unsigned long long) save_ptr);


      //printf("Push pointer to argv[0] pointer\n");
      save_ptr = *esp;
      *esp -= sizeof(char **);
      memcpy(*esp, save_ptr, sizeof(char **));
      //printf("Argv: Pointer to argv[0] pointer: 0x%llx : 0x%llx \n", (unsigned long long) *vp, (unsigned long long) save_ptr);

      //printf("Push argc\n");
      *esp -= sizeof(unsigned int);
      memcpy(*esp, &(*user_prog).arg_count, sizeof(int));
      //printf("Argc: 0x%llx : %d \n", (unsigned long long) *vp, *((unsigned int *)*vp));

      //printf("Push fake return address\n");
      *esp -= sizeof(void (*) ());
      memset(*esp, 0, sizeof(void (*) ()));

//**********************************************************************************************************************
    }
return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}


/* ADDED BY STEFANI MOORE */
void 
proc_exit (int status)
{
	struct list_elem *e;

	for(e = list_begin (&thread_current()->parent_thread->child_processes); e != list_end (&thread_current()->parent_thread->child_processes); e = list_next (e))
	{
		struct child_kernel_thread *c = list_entry (e, struct  child_kernel_thread, elem);
		if (c->tid == thread_current ()->tid)
		{
			c->was_used = true;
			c->exit_status = status;
		}

		thread_current()->exit_status = status;

		if (thread_current ()->parent_thread->waiting_on_thread == thread_current ()->tid)
			sema_up (&thread_current ()->parent_thread->child_sema);

		thread_exit ();

	}





}
