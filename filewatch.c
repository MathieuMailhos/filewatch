#define  _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <linux/limits.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>

#include <time.h>


#define MAX_EVENTS 4096
/* NAME_MAX is defined in linux/limits.h */
#define MAX_NAME_SIZE (NAME_MAX + 1)
/* A buffer big enough to read 100 inotify events in one go */
#define BUFSIZE (MAX_EVENTS * (sizeof(struct inotify_event) + MAX_NAME_SIZE))

/* Process execution mode.
 * QUEUE: commands are executed one after the other
 * CONCURRENT: commands are executed in parallel
 * OVERRIDE: last command takes over */
#define MODE_QUEUE 0
#define MODE_CONCURRENT 1
#define MODE_OVERRIDE 2

/* Macro to avoid unused warnign from gcc */
#define UNUSED(x) (void)(x)

#define MAX_PROCS 32
int children_count = 0;
pid_t children[MAX_PROCS];

/* Array of filenames watched by the program. Accessible from signal */
char **watchednames;


/* Look for first available place in a non-sorted int array.
 * Lack of optimisation here. */
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

/* Look for child pid and clean up from the array */
void remove_pid(pid_t child) {
    int i;
    for (i = 0; i < MAX_PROCS; i++) {
        if(children[i] == child) {
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

int register_files(int argc, char *argv[], char *watchednames[]) {
    int notifyd, watchfd;
    struct stat sb;
    int count_filewatch = 0;

    notifyd = inotify_init();

    if (notifyd == -1) {
        perror("inotify_init");
        return -1;
    }
    while (argc--) {
        if (stat(*argv, &sb) < 0) {
            fprintf(stderr, "Can not stat %s, ignored\n", *argv);
            continue;
        }
        /* Regular file, so add to watch list */
        if (S_ISREG(sb.st_mode)) {
            if ((watchfd = inotify_add_watch(notifyd, *argv, IN_MODIFY)) < 0) {
                fprintf(stderr, "Error adding watch for %s\n", *argv);
            } else {
                strcpy(watchednames[watchfd], *argv);
                count_filewatch++;
            }
        }
        argv++;
    }
    if (count_filewatch == 0) {
        fprintf(stderr, "No files are being watched.\n");
        return -1;
    }
    return notifyd;
}

void parse_command(char* command, char **args) {
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
    	perror("fork");
        return;
    }

    if (child) {
        add_pid(child);
	    printf("[%i] %s has been modified.\n", child, filename);
        if (mode == MODE_QUEUE) {
            wait(0);
        } else if (mode == MODE_OVERRIDE) {
            kill_children();
        }
    } else {
	    execvp(args[0], args);
	    perror(args[0]);
	    exit(EXIT_FAILURE);
    }
}

static void term_handler() {
    /* Freeing up memory allocation */
    if (watchednames != NULL) {
        for (int i = 0; i < MAX_EVENTS; i++) {
            if (watchednames[i] != NULL)
                free(watchednames[i]);
        }
        free(watchednames);
    }
    /* Option 2: wait for all childs to finish: while (wait(0) > 0); */
    kill_children();
    exit(EXIT_SUCCESS);
}

static void sigchld(int signum, siginfo_t *info, void *context) {
    printf("[%d] done.\n", info->si_pid);
    UNUSED(signum);
    UNUSED(context);
    remove_pid(info->si_pid);
}

static int sigchld_signal() {
    struct sigaction action;

    memset(&action, 0, sizeof action);
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = sigchld;
    action.sa_flags = SA_SIGINFO | SA_RESTART;

    if (sigaction(SIGCHLD, &action, NULL))
        return -1;
    return 0;
}

static int register_term_signals() {
    struct sigaction action;

    /* Avoid undefined behavior and mysterious crashes */
    memset(&action, 0, sizeof action);
    /* List of most common termination signals from `man 7 signal`. */
    int term_signals[] = { SIGHUP, SIGINT, SIGPIPE, SIGALRM, SIGTERM, SIGUSR1, SIGUSR2 };
    int term_signals_count = sizeof(term_signals)/sizeof(term_signals[0]);

    action.sa_handler = term_handler;
    sigemptyset(&action.sa_mask);

    /* Block most common termination signals to avoid breaking cleanup */
    sigemptyset(&action.sa_mask);
    for (int i = 0; i < term_signals_count; i++)
        sigaddset(&action.sa_mask, term_signals[i]);

    /* Restart system call if interrupted */
    action.sa_flags = SA_RESTART;

    for (int i = 0; i < term_signals_count; i++)
        if (sigaction(term_signals[i], &action, NULL))
            return -1;
    return 0;
}

int get_mode(int argc, char*argv[]) {
    int c;

    /* Delete this to see getopt own's error message */
    opterr = 0;
     while ( (c = getopt(argc, argv, "m:")) != EOF) {
        switch (c) {
        case 'm':
            if (strncmp(optarg, "q", 1) == 0 || strncmp(optarg, "queue", 5) == 0)
                return MODE_QUEUE;
            else if (strncmp(optarg, "c", 1) == 0 || strncmp(optarg, "concurrent", 10) == 0)
                return MODE_CONCURRENT;
            else if (strncmp(optarg, "o", 1) == 0 || strncmp(optarg, "override", 8) == 0)
                return MODE_OVERRIDE;
            else {
                fprintf(stderr, "Invalid value: %s. Choose one of [concurrent(c), queue(q), override(o)].\n", optarg);
                return -1;
            }
            break;
        case '?':
            fprintf(stderr, "Invalid option: -%c\n", optopt);
            return -1;
        }
    }
     return MODE_QUEUE;
}


int filewatch(int notifyd, char *args[64], int exec_mode) {
    int n;
    struct inotify_event *event;
    char eventbuf[BUFSIZE];  /* Events are read into here */
    char *event_iter;

    /* Start watching for modified files */
    while(1) {
        /* Read inotify events from notifyd file descriptor, into eventbuf */
        n = read(notifyd, eventbuf, BUFSIZE);
        for(event_iter = eventbuf; event_iter < eventbuf + n;) {
            event = (struct inotify_event *) event_iter;
            /* All the events are not of fixed length */
            event_iter += sizeof(struct inotify_event) + event->len;
            if (event->mask & IN_MODIFY)
                file_modified(args, watchednames[event->wd], exec_mode);
        }
    }
    return EXIT_SUCCESS;
}

int  main(int argc, char*argv[]) {
    int notifyd;
    char *args[64];
    int mode;

    mode = get_mode(argc, argv);
    if (mode == -1)
        return EXIT_FAILURE;
    argv += optind;
    argc -= optind;

    if (argc < 2) {
        fprintf(stderr, "Usage: filewatch [-m OPT] [files ...] [command]\n");
        return EXIT_FAILURE;
    }

    if (register_term_signals() || sigchld_signal()) {
        perror("signal");
        return EXIT_FAILURE;
    }

    watchednames = (char**)malloc(sizeof(char *) * MAX_EVENTS);
    for (int i = 0; i < MAX_EVENTS; i++)
        watchednames[i] = (char *)malloc(MAX_NAME_SIZE);

    notifyd = register_files(argc - 1, argv, watchednames);
    if (notifyd == -1) {
        term_handler();
        return EXIT_FAILURE;
    }
    parse_command(argv[argc - 1], args);
    return filewatch(notifyd, args, mode);
}
