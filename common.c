#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>

#include "common.h"
#include "keyboard_reader.h"
#include "screen_printer.h"
#include "message_listener.h"
#include "message_sender.h"

static pthread_t s_shutdownHelperThreadPid;

static bool s_isShutdownRequested = false;
// Used to make sure only one shutdown helper thread is created.
static pthread_mutex_t s_syncIsShutdownRequestedMutex = PTHREAD_MUTEX_INITIALIZER;

// Used to block the cleanup thread until the main thread is ready.
static pthread_barrier_t s_syncAllThreadsGoingToShutdownBarrier;

static int s_socketDescriptor = -1;
// Used to make sure only one socket is created.
static pthread_mutex_t s_syncSocketMutex = PTHREAD_MUTEX_INITIALIZER;

// Use to block the 4 worker threads until they have all been initialized.
static pthread_barrier_t s_syncAllThreadsReadyBarrier;
static bool s_barrierForAllThreadsReadyDestroyed = false;
static pthread_mutex_t s_syncBarrierForAllThreadsReadyDestroyedMutex = PTHREAD_MUTEX_INITIALIZER;

/*
 * Cleanup handler for use with pthread_cleanup_{push,pop}.
 * From the man pages:
 * After a push, this routine will be run in the following situations:
 * - The thread exits (that is, calls pthread_exit()).
 * - The thread acts upon a cancellation request.
 * - The thread calls pthread_cleanup_pop() with a non-zero execute argument.
 */
void unlockMutexesCleanup(void* whichMutex)
{
    pthread_mutex_unlock((pthread_mutex_t*) whichMutex);
}

void freeMessageFn(void* pItem)
{
    Message* pMessage = (Message*) pItem;
    if (pMessage != NULL) {
        if (pMessage->pText != NULL) {
            free(pMessage->pText);
        }
        free(pMessage);
    }
}

/*
 * Gets a socket, creating one if a socket doesn't exist.
 * Will print errors and return -1 if it fails
 */
int getSocketFdOrCreateAndBindIfDoesntExist(in_port_t ourPort)
{
    pthread_mutex_lock(&s_syncSocketMutex);

    if (s_socketDescriptor != -1) {
        pthread_mutex_unlock(&s_syncSocketMutex);
        return s_socketDescriptor;
    }
    /* Creating a socket since it doesn't exist. */

    // Set up address
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));

    // Use IPv4 networking
    sin.sin_family = AF_INET;
    // Account for differences in endianness: Use host-to-network long and short
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons(ourPort);

    // Create the socket for UDP.
    errno = 0;
    s_socketDescriptor = socket(PF_INET, SOCK_DGRAM, 0);
    if (s_socketDescriptor == -1) {
        printf("Failed to create socket: %s\n", strerror(errno));
        pthread_mutex_unlock(&s_syncSocketMutex);
        return -1;
    }

    // Bind the socket the selected port.
    errno = 0;
    bool isBindFailed = bind(s_socketDescriptor, (struct sockaddr*) &sin, sizeof(sin)) == -1;
    if (isBindFailed) {
        printf("Failed to bind port: %s\n", strerror(errno));
        close(s_socketDescriptor);
        return -1;
    }
    pthread_mutex_unlock(&s_syncSocketMutex);
    return s_socketDescriptor;
}

/*
 * Determines if the message buffer has a termination line, and then marks
 * the rest of the message as unneeded if there is a termination line.
 * If pSizeOfMessage is not NULL, *pSizeOfMessage will be set to the size
 * of message determined by scanning.
 */
bool checkAndDiscardRestIfMessageHasTerminationLine(char* messageBuffer, size_t* pSizeOfMessage)
{
    // We assume that messageBuffer has size MSG_MAX_LEN.
    if (messageBuffer == NULL) {
        return false;
    }
    // Empty string check. Fundamentally, it doesn't have a termination line.
    if (messageBuffer[0] == '\0') {
        if (pSizeOfMessage != NULL) {
            *pSizeOfMessage = 0;
        }
        return false;
    }
    // If the line starts with termination
    if (messageBuffer[0] == '!' && messageBuffer[1] == '\n') {
        messageBuffer[3] = '\0';
        if (pSizeOfMessage != NULL) {
            *pSizeOfMessage = 3;
        }
        return true;
    }

    bool isTerminationLinePresent = false;
    size_t i;
    for (i = 0; i < MSG_MAX_LEN - 1; i++) {
        // Search for a line which has just a "!<enter>", checking two adjacent
        // characters at a time.
        if (messageBuffer[i] == '!' && messageBuffer[i + 1] == '\n') {
            // Making sure this is a standalone line if we are not checking the beginning
            // Note: In here, we know that i > 0, since the condition
            // messageBuffer[0] == '!' && messageBuffer[1] == '\n' is false, since we
            // got past the guard, "If the line starts with termination"
            bool isExclamationOnItsOwnLine = (messageBuffer[i - 1] == '\n');
            if (isExclamationOnItsOwnLine) {
                if (i + 2 < MSG_MAX_LEN) {
                    // Make it so that we discard portions beyond "!<enter>".
                    messageBuffer[i + 2] = '\0';
                }

                isTerminationLinePresent = true;
                break;
            }
        }

        // If we reached the end of the string, stop searching
        if (messageBuffer[i] == '\0' || messageBuffer[i + 1] == '\0') {
            break;
        }
    }
    if (pSizeOfMessage != NULL) {
        // If the termination line is present, we need to add 1 more to the
        // returned length, since we added an extra null character at the end.
        *pSizeOfMessage = isTerminationLinePresent ? i + 2 : i + 1;
        assert(strnlen(messageBuffer, MSG_MAX_LEN) == *pSizeOfMessage);
    }
    return isTerminationLinePresent;
}

ShutdownStatus shutdownThreadWithPid(pthread_t threadPid)
{
    ShutdownStatus returnStatus = SUCCESSFUL_JOIN;
    int status = pthread_cancel(threadPid);
    if (status != 0) {
        if (status != ESRCH) {
            returnStatus = ALREADY_CANCELLED;
        }
    }

    status = pthread_join(threadPid, NULL);
    if (status != 0) {
        return JOIN_ERROR;
    } else {
        return returnStatus;
    }
}

static void printShutdownStatusErrors(char* threadName, ShutdownStatus shutdownStatus) {
    switch (shutdownStatus) {
        case CANCEL_ERROR:
            printf(" %s has failed to cancel\n", threadName);
            break;
        case JOIN_ERROR:
            printf(" %s has failed to join\n", threadName);
            break;
        case SUCCESSFUL_JOIN:
            // Pass through
        case ALREADY_CANCELLED:
            // Pass through
        default:
            break;
    }
}

static void* shutdownOfAllThreadsForProgram(void* unused)
{
    // Let the main thread know that this thread has started.
    // This will unblock the main thread as soon as the main
    // thread is also blocked on this barrier.
    pthread_barrier_wait(&s_syncAllThreadsGoingToShutdownBarrier);

    printShutdownStatusErrors("Screen printer", ScreenPrinter_shutdown());
    printShutdownStatusErrors("Keyboard reader", KeyboardReader_shutdown());
    printShutdownStatusErrors("Listener", Listener_shutdown());
    printShutdownStatusErrors("Sender", Sender_shutdown());

    if (s_socketDescriptor != -1) {
        errno = 0;
        int status = close(s_socketDescriptor);
        if (status == -1) {
            printf("Failed to close socket: %s\n", strerror(errno));
        }
    }

    // Shutdown complete!
    return NULL;
}

void initBarriers()
{
    // Barrier will block until 4 threads wait on it.
    pthread_barrier_init(&s_syncAllThreadsReadyBarrier, NULL, 4);

    // Barrier will block until 2 threads wait on it.
    pthread_barrier_init(&s_syncAllThreadsGoingToShutdownBarrier, NULL, 2);
}

void waitForAllThreadsReadyBarrier()
{
    // Barrier will block until all 4 threads call this. If we don't do this,
    // then there can be cases where a shutdown is initiated before
    // a thread has finished spawning. This means we can run into
    // situations where a thread is created after all the other threads
    // have been requested to shutdown.
    // We cannot have more than one process using pthread_join at a time,
    // as that is undefined behavior.
    // Since the main thread will be using pthread_join, we have to wait
    // until all threads are created before the possibility of shutdown.
    // See man pages for details on pthread_barrier_wait.
    pthread_barrier_wait(&s_syncAllThreadsReadyBarrier);

    // Destroy this barrier as it is no longer being used.
    // Only do this once.
    pthread_cleanup_push(unlockMutexesCleanup,
                         &s_syncBarrierForAllThreadsReadyDestroyedMutex);
    pthread_mutex_lock(&s_syncBarrierForAllThreadsReadyDestroyedMutex);
    {
        if (!s_barrierForAllThreadsReadyDestroyed) {
            s_barrierForAllThreadsReadyDestroyed = true;
            pthread_barrier_destroy(&s_syncAllThreadsReadyBarrier);
        }
    }
    pthread_cleanup_pop(1);
}

/*
 * For main thread to use to block until the thread used to manage shutdowns is done.
 * This will work even if the thread doesn't exist yet.
 */
void waitForShutdownOfAllThreads()
{
    // Block the main thread here until the shutdown thread has started.
    // Otherwise, we will be joining with a thread that doesn't exist.
    pthread_barrier_wait(&s_syncAllThreadsGoingToShutdownBarrier);

    // Wait until shutdown thread is done.
    pthread_join(s_shutdownHelperThreadPid, NULL);

    pthread_barrier_destroy(&s_syncAllThreadsGoingToShutdownBarrier);
    pthread_mutex_destroy(&s_syncSocketMutex);
    pthread_mutex_destroy(&s_syncIsShutdownRequestedMutex);
    pthread_mutex_destroy(&s_syncBarrierForAllThreadsReadyDestroyedMutex);

    ScreenPrinter_destroyMutexAndCondAndFreeLists();
    KeyboardReader_destroyMutexAndCondAndFreeList();
}

/*
 * Requests a shutdown and creates a helper thread to handle shutdowns if
 * the helper does not yet exist.
 */
void requestShutdownOfAllThreadsForProgram()
{
    // Only create one thread to manage shutdown.
    bool oldShutdownRequested;
    pthread_cleanup_push(unlockMutexesCleanup, &s_syncIsShutdownRequestedMutex);
    pthread_mutex_lock(&s_syncIsShutdownRequestedMutex);
    {
        oldShutdownRequested = s_isShutdownRequested;
        s_isShutdownRequested = true;
    }
    pthread_cleanup_pop(1);
    if (oldShutdownRequested) {
        // Don't make another shutdown helper thread if it already exists.
        return;
    }

    // Creating shutdown helper thead.
    int status = pthread_create(&s_shutdownHelperThreadPid,
                                NULL,
                                shutdownOfAllThreadsForProgram,
                                NULL);
    if (status != 0) {
        printf("Fatal error: Failed to initiate shutdown thread: %s\n", strerror(status));
    }
}
