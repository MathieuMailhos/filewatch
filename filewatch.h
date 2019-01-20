#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#ifdef __cplusplus
extern "C" {
#endif

#define _POSIX_C_SOURCE 200809L
#define MAX_EVENTS 128
/* NAME_MAX is defined in linux/limits.h */
#define MAX_NAME_SIZE (NAME_MAX + 1)
/* A buffer big enough to read 100 inotify events in one go */
#define BUFSIZE (MAX_EVENTS * (sizeof(struct inotify_event) + MAX_NAME_SIZE))

/* Process execution mode.
 * SINGLE: run a single command and wait for it to finish
 * CONCURRENT: commands are executed in parallel
 * OVERRIDE: last command takes over */
#define MODE_SINGLE 0
#define MODE_CONCURRENT 1
#define MODE_OVERRIDE 2

/* Macro to avoid unused warning from gcc, notably due to the use of
 * preprocessing directive DEBUG */
#define UNUSED(x) (void)(x)

#define MAX_PROCS 32

#include <signal.h>
#include <sys/types.h>

/* Global variables to count the number of fork() and keep track of their
 * pid in an array. Kept global to be used by _sigchld handler */
int children_count = 0;
pid_t children[MAX_PROCS];

/* print errno and exit. Never called after memory allocation. */
#define handle_error(msg)                                                      \
  do {                                                                         \
    perror(msg);                                                               \
    exit(EXIT_FAILURE);                                                        \
  } while (0)

typedef struct {
  int notify_fd;
  char **watchedfiles;
} FileWatcher;

/* Look for first available place in a non-sorted int array.
 * Lack of optimisation here. */
void add_pid(pid_t child);

/* Look for child pid and clean up from the array */
void remove_pid(pid_t child);

/* Iterate over an array of pid_t and kill exisitng children */
void kill_children();

/* Use of non-options argc and argv given to the program and fill Filewatcher
 * with the given file names */
int register_files(int argc, char *argv[], FileWatcher *fw);

/* Parse command into an array of char* in args to be used later by exec */
void parse_command(char *command, char **args);

/* Execution flow once a file filename has been modified.
 * args is the command given to execvp, filename the modified file and mode
 * the execution mode (single, concurrent, override) */
void file_modified(char *args[], char *filename, int mode);

/* register_signalfd returns a new signal file descriptor */
static int register_signalfd();

/* register a signal handler for SIGCHLD, using sigaction */
static void register_sigchld();

/* _sigchld the handler for SIGCHLD signals */
static void _sigchld(int signum, siginfo_t *info, void *context);

/* get_mode parse argc and argv given to the program and use getopt to retrieve
 * the mode given by the user. Default to single */
int get_mode(int argc, char *argv[]);

/* filewatcher_constructor is in charge of initializing the memory and the
 * file descriptor of a filewatcher struct that is later returned */
FileWatcher *filewatcher_constructor();

/* filewatcher_destructor is in charge of freeing memory allocated to the
 * given filewatcher and to close its file descriptor */
void filewatcher_destructor(FileWatcher *fw);

/* filewatch is the main looping function waiting for events from
 * inotify to trigger a command args in mode exec_mode or a signal to return */
void filewatch(FileWatcher *fw, char *args[64], int exec_mode);

#ifdef __cplusplus
}
#endif

#endif /* _THREADPOOL_H_ */
