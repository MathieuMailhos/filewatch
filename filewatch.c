#include "filewatch.h"

#include <fcntl.h>
#include <linux/limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

void add_pid(pid_t child) {
  int i;
  for (i = 0; i < MAX_PROCS; i++) {
    if (children[i] == 0) {
      children[i] = child;
      children_count++;
      return;
    }
  }
}

void remove_pid(pid_t child) {
  int i;
  for (i = 0; i < MAX_PROCS; i++) {
    if (children[i] == child) {
      children[i] = 0;
      children_count--;
      return;
    }
  }
}

void kill_children() {
  for (int i = 0; i < MAX_PROCS; i++)
    if (children[i])
      kill(children[i], SIGTERM);
}

int register_files(int argc, char *argv[], FileWatcher *fw) {
  int watchfd;
  struct stat sb;
  int count_filewatch = 0;

  while (argc--) {
    if (stat(*argv, &sb) < 0) {
      fprintf(stderr, "Can not stat %s, ignored\n", *argv);
      continue;
    }
    /* Regular file, so add to watch list */
    if (S_ISREG(sb.st_mode)) {
      if ((watchfd = inotify_add_watch(fw->notify_fd, *argv, IN_MODIFY)) < 0) {
        fprintf(stderr, "Error adding watch for %s\n", *argv);
      } else {
        strcpy(fw->watchedfiles[watchfd], *argv);
        count_filewatch++;
      }
    }
    argv++;
  }
  if (count_filewatch == 0) {
    fprintf(stderr, "No files are being watched.\n");
    return -1;
  }
  return count_filewatch;
}

void parse_command(char *command, char **args) {
  char **next = args;
  char *temp = strtok(command, " \t\n");
  while (temp != NULL) {
    *next++ = temp;
    temp = strtok(NULL, " \t\n");
  }
  *next++ = NULL;
}

void file_modified(char *args[], char *filename, int mode) {
  pid_t child;

  if (mode == MODE_CONCURRENT && children_count >= MAX_PROCS) {
    fprintf(stderr, "Max number of processes reached (%d).\n", children_count);
    return;
  }

  child = fork();
  if (child == -1) {
    handle_error("fork");
    return;
  }

  if (child) {
    add_pid(child);
#ifdef DEBUG
    printf("[%i] %s has been modified.\n", child, filename);
#else
    UNUSED(filename);
#endif

    if (mode == MODE_SINGLE) {
      wait(0);
    } else if (mode == MODE_OVERRIDE) {
      kill_children();
    }
  } else {
    execvp(args[0], args);
    handle_error(args[0]);
    exit(EXIT_FAILURE);
  }
}

static int register_signalfd() {
  sigset_t mask;
  int sfd;

  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGHUP);
  sigaddset(&mask, SIGTERM);
  if (sigprocmask(SIG_BLOCK, &mask, NULL) == -1)
    handle_error("sigprocmask");

  sfd = signalfd(-1, &mask, 0);
  if (sfd == -1)
    handle_error("signalfd");
  return sfd;
}

static void _sigchld(int signum, siginfo_t *info, void *context) {
#ifdef DEBUG
  printf("[%d] done.\n", info->si_pid);
#endif

  UNUSED(signum);
  UNUSED(context);
  remove_pid(info->si_pid);
}

static void register_sigchld() {
  struct sigaction action;

  memset(&action, 0, sizeof action);
  sigemptyset(&action.sa_mask);
  action.sa_sigaction = _sigchld;
  action.sa_flags = SA_SIGINFO | SA_RESTART;

  if (sigaction(SIGCHLD, &action, NULL))
    handle_error("sigaction");
}

int get_mode(int argc, char *argv[]) {
  int c;

  /* Delete this to see getopt own's error message */
  opterr = 0;
  while ((c = getopt(argc, argv, "m:")) != EOF) {
    switch (c) {
    case 'm':
      if (strncmp(optarg, "s", 1) == 0 || strncmp(optarg, "single", 5) == 0)
        return MODE_SINGLE;
      else if (strncmp(optarg, "c", 1) == 0 ||
               strncmp(optarg, "concurrent", 10) == 0)
        return MODE_CONCURRENT;
      else if (strncmp(optarg, "o", 1) == 0 ||
               strncmp(optarg, "override", 8) == 0)
        return MODE_OVERRIDE;
      else {
        fprintf(stderr,
                "Invalid value: %s. Choose one of [single(s), concurrent(c), "
                "override(o)].\n",
                optarg);
        return -1;
      }
      break;
    case '?':
      fprintf(stderr, "Invalid option: -%c\n", optopt);
      return -1;
    }
  }
  return MODE_SINGLE;
}

FileWatcher *filewatcher_constructor() {
  FileWatcher *fw;
  int notifyd;
  char **wf;

  notifyd = inotify_init();
  if (notifyd == -1) {
    handle_error("inotify_init");
    exit(EXIT_FAILURE);
  }

  fw = (FileWatcher *)malloc(sizeof(FileWatcher));

  fw->notify_fd = notifyd;
  wf = (char **)malloc(sizeof(char *) * MAX_EVENTS);
  for (int i = 0; i < MAX_EVENTS; i++)
    wf[i] = malloc(sizeof(char *) * MAX_NAME_SIZE);
  fw->watchedfiles = wf;
  return fw;
}

void filewatcher_destructor(FileWatcher *fw) {
  if (fw != NULL) {
    close(fw->notify_fd);
    if (fw->watchedfiles != NULL) {
      for (int i = 0; i < MAX_EVENTS; i++)
        free(fw->watchedfiles[i]);
      free(fw->watchedfiles);
    }
    free(fw);
    fw = NULL;
  }
}

void filewatch(FileWatcher *fw, char *args[64], int exec_mode) {
  int n;
  struct inotify_event *event;
  char eventbuf[BUFSIZE]; /* Events are read into here */
  char *event_iter;
  struct pollfd fds[2];
  struct signalfd_siginfo fdsi;
  ssize_t s;

  memset(&fds, 0, sizeof(fds));
  fds[0].fd = register_signalfd();
  fds[0].events = POLLIN;
  fds[1].fd = fw->notify_fd;
  fds[1].events = POLLIN;

  /* Start watching for modified files */
  while (1) {
    if (poll(fds, 2, -1) == -1)
      handle_error("poll");
    else if (fds[0].revents & POLLIN) {
      s = read(fds[0].fd, &fdsi, sizeof(struct signalfd_siginfo));
      if (s != sizeof(struct signalfd_siginfo))
        handle_error("read");
#ifdef DEBUG
      printf("Received %s interrupt from %d sent by %d\n",
             strsignal(fdsi.ssi_signo), fdsi.ssi_pid, fdsi.ssi_uid);
#endif
      return;
    } else if (fds[1].revents & POLLIN) {
      /* Read inotify events from notifyd file descriptor, into eventbuf */
      n = read(fds[1].fd, eventbuf, BUFSIZE);
      if (n == -1)
        handle_error("read");

      for (event_iter = eventbuf; event_iter < eventbuf + n;) {
        event = (struct inotify_event *)event_iter;
        /* All the events are not of fixed length */
        event_iter += sizeof(struct inotify_event) + event->len;
        if (event->mask & IN_MODIFY)
          file_modified(args, fw->watchedfiles[event->wd], exec_mode);
      }
    }
  }
}

int main(int argc, char *argv[]) {
  char *args[64];
  int mode;

  FileWatcher *fw = NULL;

  mode = get_mode(argc, argv);
  if (mode == -1)
    return EXIT_FAILURE;
  argv += optind;
  argc -= optind;

  if (argc < 2) {
    fprintf(stderr, "Usage: filewatch [-m OPT] [files ...] [command]\n");
    return EXIT_FAILURE;
  }

  register_sigchld();
  fw = filewatcher_constructor();

  if (register_files(argc - 1, argv, fw) < 1) {
    fprintf(stderr, "No files to watch\n");
    filewatcher_destructor(fw);
  }
  parse_command(argv[argc - 1], args);
  filewatch(fw, args, mode);
  filewatcher_destructor(fw);
  kill_children();
  return EXIT_SUCCESS;
}
