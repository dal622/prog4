/*
 *  CSE202: Processes and Signals
 *  Full name: Danny Lin
 *  Full Lehigh Email Address: dal622@lehigh.edu
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <wait.h>

#define N 4096

/* Global variable shared between the main process and the signal handler */
volatile sig_atomic_t sum = 0;
volatile sig_atomic_t num_signals = 0;

/* function to initialize an array of integers */
void initialize(int *);
/* Wrapper functions for all system calls */
void unix_error(const char *msg);
pid_t Fork();
pid_t Wait(int *status);
pid_t Waitpid(pid_t pid, int *status, int options);
int Sigqueue(pid_t pid, int signum, union sigval value);
int Sigemptyset(sigset_t *set);
int Sigfillset(sigset_t *set);
int Sigaction(int signum, const struct sigaction *new_act,
              struct sigaction *old_act);
int Sigprocmask(int how, const sigset_t *set, sigset_t *oldset);
ssize_t Write(int d, const void *buffer, size_t nbytes);
ssize_t Read(int d, void *buffer, size_t nbytes);
typedef void handler_t;
handler_t *Signal(int signum, handler_t *handler);
void sigusr2_handler(int sig, siginfo_t *info, void *context);

/* main function */
int main() {
  // initialize array A[] such that each subsequent number is +1 the previous
  int A[N];
  initialize(A);
  /* install the SIGUSR2 handler using Signal (portable handler) */
  fprintf(stderr, "Parent process %d installing SIGUSR2 handler\n", getpid());
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sa.sa_sigaction = sigusr2_handler;
  sigemptyset(&sa.sa_mask);
  Sigaction(SIGUSR2, &sa, NULL);

  // Block the SIGUSR2 signal
  sigset_t mask;
  Sigemptyset(&mask);
  sigaddset(&mask, SIGUSR2);
  Sigprocmask(SIG_BLOCK, &mask, NULL);

  /* create (P-1) processes (macro P defined at compile time) */
  for (int i = 0; i < P - 1; i++) {
    pid_t pid = Fork();
    if (pid == 0) { // child process
      // child processes need to calculate the partial sum by adding elements
      // from index x to index y
      int partial_sum = 0;
      fprintf(stderr,
              "Child process %d adding the elements from index %d to %d\n",
              getpid(), i * N / P, (i + 1) * N / P);

      for (int j = i * N / P; j < (i + 1) * N / P; j++) {
        partial_sum += A[j];
      }

      // child processes send signal back to parent with calculated partial sum
      union sigval value;
      value.sival_int = partial_sum;

      // send signal back to parent
      Sigqueue(getppid(), SIGUSR2, value);

      exit(0);
    }
    // Unblock the SIGUSR2 signal
    Sigprocmask(SIG_UNBLOCK, &mask, NULL);
  }

  // Wait for all child processes to finish sending signals
  while (num_signals < P - 1) {
    sleep(1);
  }

  // reap child processes
  int status;
  for (int i = 0; i < P - 1; i++) {
    sleep(3);
    Wait(&status);
  }

  // Parent process
  int partial_sum = 0;
  fprintf(stderr, "Parent process %d adding the elements from index %d to %d\n",
          getpid(), (P - 1) * N / P, N);
  // Parent process adds the last section of the array and saves into
  // partial_sum
  for (int i = (P - 1) * N / P; i < N; i++) {
    partial_sum += A[i];
  }

  fprintf(stderr, "Parent process sum = %d\n", partial_sum);

  // Parent process updates the global sum variable and prints out the result
  sum += partial_sum;
  fprintf(stderr, "Final Sum = %d\n", sum);

  return 0;
}

/* this method initializes the array such that each subsequent element is one
 * more than the last */
void initialize(int *M) {
  for (int i = 0; i < N; i++) {
    M[i] = i + 1;
  }
}

/* this method handles the signal sent by the child process(es) */
void sigusr2_handler(int sig, siginfo_t *info, void *context) {
  // when parent gets a signal we want to update the global sum to include the
  // partial sum
  sum += info->si_value.sival_int;
  // print to track if working
  printf("Parent process caught SIGUSR2 with partial sum: %d\n",
         info->si_value.sival_int);
  num_signals++;
}

/* this method creates the new child process */
pid_t Fork() {
  pid_t pid;
  if ((pid = fork()) < 0) {
    unix_error("Fork error");
  }
  return pid;
}

/* this method causes the parent process to wait until a desired status is
 * returned from the child process */
pid_t Wait(int *status) {
  pid_t pid = wait(status);
  // print to track if working
  if (pid > 0) {
    if (WIFEXITED(*status)) {
      printf("Child process %d terminated normally with exit status %d\n", pid,
             WEXITSTATUS(*status));
    } else {
      printf("Child process %d terminated abnormally\n", pid);
    }
  } else {
    printf("Child process %d terminated abnormally\n", pid);
  }
  return pid;
}

/* this method is not used in the main method, but waits until a specific child
 * process returns a desired status */
pid_t Waitpid(pid_t pid, int *status, int options) {
  // options = 0, suspend the parent until child is term, options = WNOHANG,
  // return immediately if none of the child processes has term, options =
  // WUNTRACED, suspend execution until a child process term/stopped, options =
  // WCONTINUED, suspend execution until a child process is termination/stopped.
  pid_t ret_pid = waitpid(pid, status, options);
  if (ret_pid < 0) {
    unix_error("waitpid error");
  }
  return ret_pid;
}

/* this method allows the child process to send back its signal */
int Sigqueue(pid_t pid, int signum,
             union sigval value) { // Sigqueue(getppid(), SIGUSR2, value);
  // print to track if working
  fprintf(stderr,
          "Child process %d sending SIGUSR2 to parent process with the "
          "partial sum %d\n",
          getpid(), signum);

  int ret = sigqueue(pid, signum, value);
  if (ret < 0) {
    unix_error("sigqueue error");
  }
  return ret;
}

/* this method intialize a signal mask to exclude all signals */
int Sigemptyset(sigset_t *set) {
  int ret = sigemptyset(set);
  if (ret < 0) {
    unix_error("sigemptyset error");
  }
  return ret;
}

/* this method initialize a signal mask to include all signals */
int Sigfillset(sigset_t *set) {
  int ret = sigfillset(set);
  if (ret < 0) {
    unix_error("sigfillset error");
  }
  return ret;
}

/* this method allows the calling process to examine the action to be associated
 * with a specific signal */
int Sigaction(int signum, const struct sigaction *new_act,
              struct sigaction *old_act) {
  int ret = sigaction(signum, new_act, old_act);
  if (ret < 0) {
    unix_error("sigaction error");
  }
  return ret;
}

/* this method allows the child process to send back its signal */
int Sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
  int ret = sigprocmask(how, set, oldset);
  if (ret < 0) {
    unix_error("sigprocmask error");
  }
  return ret;
}

/* this method also isn't used, but attempts to write data */
ssize_t Write(int d, const void *buffer, size_t nbytes) {
  ssize_t ret = write(d, buffer, nbytes);
  if (ret < 0) {
    unix_error("write error");
  }
  return ret;
}

/* this method also isn't used, but attempts to read in data */
ssize_t Read(int d, void *buffer, size_t nbytes) {
  ssize_t ret = read(d, buffer, nbytes);
  if (ret < 0) {
    unix_error("read error");
  }
  return ret;
}

/* this method handles all signals */
handler_t *Signal(int signum, handler_t *handler) {
  struct sigaction new_act, old_act;
  new_act.sa_handler = handler;
  Sigemptyset(&new_act.sa_mask);
  new_act.sa_flags = SA_RESTART;

  if (Sigaction(signum, &new_act, &old_act) < 0) {
    unix_error("Signal error");
  }
  return old_act.sa_handler;
}

/* general collect all for errors that may come up */
void unix_error(const char *msg) {
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  exit(0);
}