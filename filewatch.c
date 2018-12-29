#include <stdio.h>
#include <stdlib.h>
#include <sys/inotify.h>

/* A buffer big enough to read 100 events in one go */
#define BUFSIZE (100 * (sizeof(struct inotify_event) + NAME_MAX + 1))

int  main(int argc, char*argv[]) {
    int notifyd, watchfd;
    char watchednames[100][NAME_MAX+1];

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <cmd> [[FILES]]\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    notifyd = inotify_init(); //Not checked for error

    for(int i = 1; i < argc - 1; i++) {
        if (stat(argv[i], &sb) < 0) {
            fprintf(stderr, "Can not stat %s, ignored\n", argv[i]);
            continue;
        }
        /* Regular file, so add to watch list */
        if (S_ISREG(sb.st_mode)) {
            if (watchfd = inotify_add_watch(notifyd, argv[i], IN_MODIFY) < 0) {
                fprintf(stderr, "Error adding watch for %s\n", argv[i]);
            } else {
                strcpy(watchednames[watchfd], argv[i]);
                printf("Added %s to watch list on descriptor %d\n", argv[i], watchfd);
            }
        }
    }

    return 0;
}
