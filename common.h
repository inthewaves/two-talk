#ifndef _COMMON_FUNCS_CONSTANTS_H_
#define _COMMON_FUNCS_CONSTANTS_H_

#include <stdbool.h>
#include <netdb.h>

// Max size for a UDP packet.
#define MSG_MAX_LEN 65507

typedef struct Message_s Message;
struct Message_s {
    char* pText;
    bool isShutdownMessage;
};

typedef enum {
    SUCCESSFUL_JOIN,
    ALREADY_CANCELLED,
    CANCEL_ERROR,
    JOIN_ERROR
} ShutdownStatus;

void unlockMutexesCleanup(void* whichMutex);

void freeMessageFn(void* pItem);

ShutdownStatus shutdownThreadWithPid(pthread_t threadPid);

void initBarriers();

void waitForAllThreadsReadyBarrier();

/*
 * Gets a socket, creating one and binding it if a socket doesn't exist.
 * Returns -1 on error.
 */
int getSocketFdOrCreateAndBindIfDoesntExist(in_port_t ourPort);

/*
 * Returns true if the message has a line that is just "!\n", and false if not.
 */
bool checkAndDiscardRestIfMessageHasTerminationLine(char* messageBuffer, size_t* pSizeOfMessage);

/*
 * Wait until the thread used to manage shutdowns is done.
 * Used by the main thread to block itself.
 */
void waitForShutdownOfAllThreads();

/*
 * Requests a shutdown of all threads
 */
void requestShutdownOfAllThreadsForProgram();

#endif //_COMMON_FUNCS_CONSTANTS_H_
