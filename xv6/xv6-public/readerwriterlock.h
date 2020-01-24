// Mutual exclusion lock.

struct readerwriterlock {
    uint locked;           // Is the lock held?
    struct ticketlock mutex;   // spinlock protecting this sleep lock
    struct ticketlock writer;
    int pattern[100];


    // For debugging:
    char *name;        // Name of lock.
    int pid;          // Process holding lock
    // that locked the lock.
};

