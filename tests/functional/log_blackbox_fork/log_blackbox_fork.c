/*
 * Copyright 2018 Red Hat, Inc.
 *
 * All rights reserved.
 *
 * Author: Jan Pokorny <jpokorny@redhat.com>
 *
 * This file is part of libqb.
 *
 * libqb is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * libqb is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libqb.  If not, see <http://www.gnu.org/licenses/>.
 */

#define _GNU_SOURCE
#include <limits.h>
#include <poll.h>
#include <sched.h>  /* clone */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  /* execl, sleep */

#include <sys/resource.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <qb/qblog.h>

#define TRACE(fmt, ...) \
	fprintf(stderr, "[%ld] %s: " fmt "\n", (long) getpid(), \
	        __func__, __VA_ARGS__)
#define TRACE1(str)    TRACE("%s", str)
#define TRACE_ENTER()  TRACE1("enter")
#define TRACE_LEAVE()  TRACE1("leave")

#define WAITPID_HANDLE(desc, pid, return_after) do {		\
	int _wstatus;						\
	TRACE("waiting for %s (PID %ld)", desc, (long) pid);	\
	if (waitpid(pid, &_wstatus, 0) == -1) {			\
		perror("waitpid " desc);			\
		return EXIT_FAILURE;				\
	}							\
	if (WIFEXITED(_wstatus)) {				\
		TRACE("%s (PID %ld) terminated with EC=%d",	\
		      desc, (long) pid, WEXITSTATUS(_wstatus));	\
		if (return_after) return WEXITSTATUS(_wstatus);	\
	} else if (WIFSIGNALED(_wstatus)) {			\
		TRACE("%s (PID %ld) signalled with SIGNAL=%d",	\
		      desc, (long) pid, WTERMSIG(_wstatus));	\
		if (return_after) return EXIT_FAILURE;		\
	} else {						\
		TRACE("%s (PID %ld) terminated (unhandled)",	\
		      desc, (long) pid);			\
		if (return_after) return EXIT_FAILURE;		\
	}} while (0)

#define STACK_SIZE	(1024 * 1024)
#define BLACKBOX_SIZE	(1024 * 16)

typedef struct {
	const char *argv0;
	rlim_t nproc_rlim_cur;
	size_t iters;
} passaround_t;

static volatile sig_atomic_t child_ready;
static volatile sig_atomic_t child_gone;
static pid_t original_ppid;

/*
 * Blocks for sequence under test
 */

static int32_t
sequence_under_test_prep(void)
{
	int32_t rc;
	qb_log_init("test-blackbox", LOG_USER, LOG_INFO);
	rc = qb_log_filter_ctl(QB_LOG_BLACKBOX, QB_LOG_FILTER_ADD,
			  QB_LOG_FILTER_FILE, "*", LOG_TRACE);
	rc |= qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, BLACKBOX_SIZE);
	rc |= qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE);
	return rc;
}

static inline void
sequence_under_test_log_something(void)
{
	qb_log(LOG_TRACE, "just some trace message: %ld", (long) getpid());
}

#if 0
static void
sequence_under_test_finalize(void)
{
	qb_log_fini();
}
#endif

/*
 * Test composition itself
 */

static void handle_detached_child_ready(int signo) {
	child_ready = 1;
}

static void handle_remote_child_passing(int signo) {
	child_gone = 1;
}

static void handle_original_crash(int signo) {
	TRACE("hit with signal %d", signo);
	signal(signo, SIG_DFL);
	kill(original_ppid, SIGUSR1);
	raise(signo);
}

static int
test_run_detaching(void)
{
	pid_t workpid;
	sigset_t sig_blocked, sig_orig;
#ifndef NFIX
	int32_t qb_ret;
#endif
	TRACE_ENTER();

	sequence_under_test_prep();
	original_ppid = getppid();

	if (sigemptyset(&sig_blocked) != 0
	    || sigaddset(&sig_blocked, SIGUSR1) != 0
	    || sigprocmask(SIG_BLOCK, &sig_blocked, &sig_orig) != 0) {
		perror("Couldn't arrange signal handling around forking");
		return EXIT_FAILURE;
	}

	switch ((workpid = fork())) {
		case -1:
			perror("fork to get detached logging child");
			return EXIT_FAILURE;
			break;
		case 0:
			TRACE1("child");
			/*
			 * child which is disconnected, run this process
			 */
			sigprocmask(SIG_SETMASK, &sig_orig, NULL);
			workpid = getppid();
			break;
		default:
			TRACE1("parent");
			signal(SIGUSR1, handle_detached_child_ready);
			do {
				sigsuspend(&sig_orig);
				if (child_ready) {
					break;
				}
			} while (kill(workpid, 0) == 0);
			if (!child_ready) {
				fprintf(stderr, "Child died unexpectedly upon fork\n");
				exit(EXIT_FAILURE);
			}
			return EXIT_SUCCESS;
			break;
	}

#ifndef NFIX
	TRACE1("looking if new API meaning acknowledged");
	qb_ret = qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_SIZE, BLACKBOX_SIZE);
	if (qb_ret == 1) {
		/* we are done */
		TRACE1("new API meaning works as expected");
	} else if (qb_ret < 0
	           || (qb_ret = qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_FALSE)) < 0
	           || (qb_ret = qb_log_ctl(QB_LOG_BLACKBOX, QB_LOG_CONF_ENABLED, QB_TRUE)) < 0) {
		fprintf(stderr,
		    "Unable to reinitialize log flight recorder. " \
		    "The most common cause of this error is " \
		    "not enough space on /dev/shm. This will continue work, " \
		    "but blackbox will not be available\n");
	}
#endif

	signal(SIGFPE, handle_original_crash);
	signal(SIGBUS, handle_original_crash);
	signal(SIGSEGV, handle_original_crash);

	if (workpid != 1 && workpid == getppid()) {
		TRACE("signalling to parent %ld", (long) workpid);
		kill(workpid, SIGUSR1);
	}
	while (1) {
		sequence_under_test_log_something();
	}
	return EXIT_SUCCESS;
}

static int
test_run_clashing(const passaround_t *pass, pid_t pid_to_clash)
{
	int wstatus;
	pid_t pid;

	TRACE_ENTER();

	while ((pid = fork()) > 0 && pid != pid_to_clash) {
#if 0
		TRACE("forked %d", pid);
#endif
		waitpid(pid, &wstatus, 0);
	}
	if (pid < 0) {
		perror("fork to get clashing PID");
		exit(EXIT_FAILURE);
	} else if (pid == 0 && getpid() != pid_to_clash) {
#if 0
		TRACE("dropping PID %d", pid);
#endif
		exit(EXIT_SUCCESS);
	} else if (pid == pid_to_clash) {
		TRACE("hit on PID %ld", (long) pid);
		WAITPID_HANDLE("clashing PID", pid, 1);
		assert(0);
		return EXIT_SUCCESS;
	} else {
		TRACE1("rerunning self");
		if (execl(pass->argv0, pass->argv0, "trigger", NULL) == -1) {
			perror("rerunning self");
			exit(EXIT_FAILURE);
		}
	}

	return EXIT_FAILURE;
}

static int
test_run(const passaround_t *pass)
{
	pid_t pid;
	size_t iters = 0;
	sigset_t sig_blocked, sig_orig;

	TRACE_ENTER();

	if (sigemptyset(&sig_blocked) != 0
	    || sigaddset(&sig_blocked, SIGUSR1) != 0
	    || sigprocmask(SIG_BLOCK, &sig_blocked, &sig_orig) != 0) {
		perror("Couldn't arrange signal handling around forking");
		return EXIT_FAILURE;
	}

	if ((pid = fork()) == 0) {
		/* run first blackbox user */
		sigprocmask(SIG_SETMASK, &sig_orig, NULL);
		exit(test_run_detaching());
	} else if (pid > 0) {
		/* run the other to reach the same blackbox-PID */
		WAITPID_HANDLE("process to simulate regular access", pid, 0);

		signal(SIGUSR1, handle_remote_child_passing);
		sigprocmask(SIG_SETMASK, &sig_orig, NULL);

		while (test_run_clashing(pass, pid) == EXIT_SUCCESS
		       && !child_gone) {
			if (++iters > pass->iters) {
				fprintf(stderr, "returning after %zu"
				                " unsuccessful iterations\n",
						iters);
				return EXIT_FAILURE;
			}
			if (waitpid(-1, NULL, WNOHANG) == -1) {
				perror("opportunistic waitpid");
				return errno;
			}
			TRACE("finished iteration %zu/%zu", iters, pass->iters);
		};
		fprintf(stderr, "returning successfully after %zu iterations\n",
				iters);
	}
	return EXIT_SUCCESS;
}

static int
test_toplevel(void *passaround)
{
	pid_t pid;
	int rc;
	const passaround_t *const pass = passaround;
	rlim_t counter;

	TRACE_ENTER();
	assert(pass != NULL && pass->argv0 != NULL);

	/* we exhaust just 2/7 of the process limit to make the hit
	   more likely (pays off when called repeatedly) */
	counter = (pass->nproc_rlim_cur == RLIM_INFINITY)
	        ? ((rlim_t) 1) << (sizeof(rlim_t) * CHAR_BIT - 2)
	        : pass->nproc_rlim_cur / 7 * 2;
	TRACE("counter is %lu", (unsigned long) counter);

	pid = getpid();
	do {
		if (counter == 0) {
			break;
		}
		counter--;
	} while ((pid = fork()) > 0);

	if (pid < 0 || (counter == 0 && pid != 0)) {
		/* parent */
		if (pid < 0) {
			perror("fork to semi-fill PID range");
			fprintf(stderr, "counter is %lu\n", counter);
			exit(EXIT_FAILURE);
		}
		TRACE("counter is %lu, pid is %ld", counter, (long) pid);
		rc = test_run(pass);
		TRACE1("killing");
		kill(0, SIGTERM);
		return rc;
	}

  	/* child: wait forever (will be knocked down, eventually) */
	poll(NULL, 0, -1);

	return EXIT_SUCCESS;
}

static int
test_side_trigger(void) {
	int32_t rc;
	TRACE_ENTER();
	rc = sequence_under_test_prep();
#if 0
	/* just qb_log_init is enough alone */
	sequence_under_test_log_something();
	sequence_under_test_finalize();
#endif
	TRACE_LEAVE();
	return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int
main(int argc, char *argv[])
{
	char *stack;
	pid_t pid;
	passaround_t pass;
	struct rlimit lim;

	if (argc > 1 && !strcmp(argv[1], "trigger")) {
		/* executed in the role of the inner helper of test,
		   assuming everything is set up (expected PID, euid) */
		return test_side_trigger();
	} else if (geteuid() != 0) {
		fprintf(stderr, "cannot use PID namespace (not root)\n");
		pass.iters = 10;
	} else {
#ifndef NPIDNS
		pass.iters = 1;
#else
		fprintf(stderr, "cannot use PID namespace\n");
		pass.iters = 10;
#endif
	}

	pass.argv0 = argv[0];
	pass.nproc_rlim_cur = 1050;  /* means exhausting by 300 */
	if (pass.iters > 1 && getrlimit(RLIMIT_NPROC, &lim) == -1) {
        	perror("getrlimit");
		fprintf(stderr, "just static PID preallocating for a speedup\n");
	} else if (pass.iters > 1) {
		pass.nproc_rlim_cur = lim.rlim_cur;
	}

	if (pass.iters == 1) {
		/* we use a separate PID namespace so that surrounding processes
		   are not intefering with our objective of getting exact hit on
		   recycled PID */
		if ((stack = malloc(STACK_SIZE)) == NULL) {
        		perror("malloc");
			return EXIT_FAILURE;
		}
#ifndef STACK_UPWARDS
		stack += STACK_SIZE;
#endif
		TRACE1("going to clone(,, CLONE_NEWPID)");
		pid = clone(&test_toplevel, stack, CLONE_NEWPID | SIGCHLD, &pass);
	} else {
		return test_toplevel(&pass);
	}

	if (pid == -1) {
		perror("clone");
		return EXIT_FAILURE;
	}
   	TRACE("PID 1 of new namespace maps to PID %ld outside", (long) pid);
	WAITPID_HANDLE("PID 1 of new namespace", pid, 1);

	assert(0);
	return EXIT_SUCCESS;
}
