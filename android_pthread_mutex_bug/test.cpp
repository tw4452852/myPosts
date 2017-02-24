#include <pthread.h>
#include <stdio.h>

static void*
work(void *arg)
{
	pthread_exit(NULL);
}

int
main(int argc, const char *argv[])
{
	pthread_t t;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_create( &t, &attr, &work, NULL);

	while (1) {
		sleep(1);
	}

	printf("well done\n");
	return 0;
}

Breakpoint 3, _pthread_internal_remove_locked (thread=0x7ffff741c300) at bionic/libc/bionic/pthread_internals.cpp:39
(gdb) set thread->tid=999
(gdb) p *thread
$1 = {next = 0x7ffff7ffd500 <__dl__ZZ15__libc_init_tlsR19KernelArgumentBlockE11main_thread>, prev = 0x0, tid = 999, cached_pid_ = 12774, tls = 0x7ffff7e65b60, attr = {flags = 1, 
    stack_base = 0x7ffff7d68000, stack_size = 1040384, guard_size = 4096, sched_policy = 0, __sched_priority = 0, __reserved = "\001\000\000\000\000\000\000\000\060IUUUU\000"}, cleanup_stack = 0x0, 
  start_routine = 0x555555554a60 <work2(void*)>, start_routine_arg = 0x0, return_value = 0x0, alternate_signal_stack = 0x0, startup_handshake_mutex = {value = 1, 
    __reserved = '\000' <repeats 35 times>}, dlerror_buffer = '\000' <repeats 511 times>}

Breakpoint 2, art::sigprocmask (how=2, bionic_new_set=0x7ffff7e65ad0, bionic_old_set=0x0) at art/sigchainlib/sigchain.cc:224
224     extern "C" int sigprocmask(int how, const sigset_t* bionic_new_set, sigset_t* bionic_old_set) {
(gdb) p *__get_thread()
$5 = {next = 0x7ffff7ffd500 <__dl__ZZ15__libc_init_tlsR19KernelArgumentBlockE11main_thread>, prev = 0x0, tid = 999, cached_pid_ = 12774, tls = 0x7ffff7e65b60, attr = {flags = 1, 
    stack_base = 0x7ffff7d68000, stack_size = 1040384, guard_size = 4096, sched_policy = 0, __sched_priority = 0, __reserved = "\001\000\000\000\000\000\000\000\060IUUUU\000"}, cleanup_stack = 0x0, 
  start_routine = 0x555555554a60 <work2(void*)>, start_routine_arg = 0x0, return_value = 0x0, alternate_signal_stack = 0x0, startup_handshake_mutex = {value = 1, 
    __reserved = '\000' <repeats 35 times>}, dlerror_buffer = '\000' <repeats 511 times>}

