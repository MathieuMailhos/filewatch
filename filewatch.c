#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <linux/limits.h>
#include <string.h>


/* A buffer big enough to read 100 events in one go */
#define BUFSIZE (100 * (sizeof(struct inotify_event) + NAME_MAX + 1))

int  main(int argc, char*argv[]) {
    int notifyd, watchfd;
    int n;
    struct stat sb;
    char eventbuf[BUFSIZE];  /* Events are read into here */
    char *event_iter;
    pid_t child;
    struct inotify_event *event;

    /* NAME_MAX is defined in linux/limits.h 
     * The uniform system limit (if any) for the length of a file name component, not including the terminating null character.
     */
    char watchednames[100][NAME_MAX+1];

    if (argc < 3) {
        fprintf(stderr, "Usage: %s [files ...] [command]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    notifyd = inotify_init(); //Not checked for error

    if (notifyd == -1) {
        perror("inotify_init");
        exit(EXIT_FAILURE);
    }

//    if (stat(argv[argc - 1], &sb) < 0 || sb.st_mode | S_IXUSR) {
//        fprintf(stderr, "Program '%s' not found or not executable\n", argv[argc - 1]);
//        exit(EXIT_FAILURE);
//    }

    for(int i = 1; i < argc - 1; i++) {
        if (stat(argv[i], &sb) < 0) {
            fprintf(stderr, "Can not stat %s, ignored\n", argv[i]);
            continue;
        }
        /* Regular file, so add to watch list */
        if (S_ISREG(sb.st_mode)) {
            if ((watchfd = inotify_add_watch(notifyd, argv[i], IN_MODIFY)) < 0) {
                fprintf(stderr, "Error adding watch for %s\n", argv[i]);
            } else {
                strcpy(watchednames[watchfd], argv[i]);
                printf("Added %s to watch list on descriptor %d\n", argv[i], watchfd);
            }
        }
    }

    /* Start watching for modified files */
    while(1) {
        /* Read inotify events from notifyd file descriptor, into eventbuf */
        n = read(notifyd, eventbuf, BUFSIZE);
        for(event_iter = eventbuf; event_iter < eventbuf + n;) {
            event = (struct inotify_event *) event_iter;
            /* All the events are not of fixed length */
            event_iter += sizeof(struct inotify_event) + event->len;
            if (event->mask & IN_MODIFY) {
		child = fork();
		if (child == -1) {
			perror("fork");
			continue;
		}
                if (child) {
		    printf("[%i] %s was modified.\n", child, watchednames[event->wd]);
                    wait(0);
                } else {
		    /* {"ls", "-a", 0} */
                    //execvp(argv[argc - 1], argv[argc - 1]);
                    execlp(argv[argc - 1], argv[argc - 1], (char *)0);
                    fprintf(stderr, "%s: not found\n", argv[argc - 1]);
                    exit(EXIT_FAILURE);
                }
            }
        }
    }
    return 0;
}
