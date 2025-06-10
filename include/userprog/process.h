#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H
#define FDCOUNT_LIMIT 1024

#include "threads/thread.h"

struct fork_info {
	struct intr_frame *parent_if;
	struct thread *parent;
};

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);
void argument_stack(char **argv, int argc, struct intr_frame *if_);
struct thread *get_child_process(int pid);
int process_add_file(struct file *f);
struct file *process_get_file(int fd);
int process_close_file(int fd);

#ifdef VM
bool lazy_load_segment(struct page *page, void *aux);
#endif

#endif /* userprog/process.h */
