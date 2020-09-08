#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include "keyboard_reader.h"
#include "list.h"
#include "common.h"

static pthread_t s_threadPid;
static pthread_cond_t s_syncMessagesAvailableCondVar = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t s_accessQueueMutex = PTHREAD_MUTEX_INITIALIZER;

static List* s_outMessageQueue = NULL;

static bool createMessageFromBufferAndPutOnQueue(char* messageBuffer, size_t sizeOfMessage,
                                                 bool isShutdownMessage)
{
    if (s_outMessageQueue == NULL) {
        return false;
    }

    // Account for \0 character.
    // Will be freed after it has been sent by the message sender.
    // Do not let this thread be cancelled, or pMessageText might be left unfreed.
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    char* pMessageText = malloc(sizeof(char) * (sizeOfMessage + 1));;
    memset(pMessageText, 0, sizeof(char) * (sizeOfMessage + 1));
    strncpy(pMessageText, messageBuffer, sizeOfMessage);

    Message* pMessage = malloc(sizeof(Message));
    pMessage->pText = pMessageText;
    pMessage->isShutdownMessage = isShutdownMessage;

    bool isEnqueueSuccessful;

    pthread_cleanup_push(unlockMutexesCleanup, &s_accessQueueMutex);
    pthread_mutex_lock(&s_accessQueueMutex);
    {
        if (List_append(s_outMessageQueue, pMessage) == LIST_FAIL) {
            fputs("**The sending message queue is too large!**\n", stdout);
            fputs("**Your most recent message will be dropped, please try resending**\n",
                  stdout);
            isEnqueueSuccessful = false;
            freeMessageFn(pMessage);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	} else {
            // Here, the append was successful, so the queue is not empty.
            // If the sender is blocked on this cond var, then the reader will block
            // until reader thread is done.
            pthread_cond_signal(&s_syncMessagesAvailableCondVar);
            isEnqueueSuccessful = true;
        }
    }
    // Unlock the mutex.
    pthread_cleanup_pop(1);

    return isEnqueueSuccessful;
}

static void* KeyboardReader_run(void* stub)
{
    waitForAllThreadsReadyBarrier();

    if (s_outMessageQueue == NULL) {
        fputs("KeyboardReader_run: error: message list is NULL\n", stderr);
    }

    char messageBuffer[MSG_MAX_LEN];
    while (1) {
        memset(messageBuffer, 0, sizeof(char) * MSG_MAX_LEN);

        read(STDIN_FILENO, messageBuffer, MSG_MAX_LEN);

        if (messageBuffer[0] == '\0') {
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            requestShutdownOfAllThreadsForProgram();
            break;
        }

        // Discard parts of the message that are not needed.
        size_t sizeOfMessage = 0;
        bool isCancellationMessage = checkAndDiscardRestIfMessageHasTerminationLine(messageBuffer,
                                                                                    &sizeOfMessage);

        bool isEnqueueSuccessful = createMessageFromBufferAndPutOnQueue(messageBuffer,
                                                                         sizeOfMessage,
                                                                         isCancellationMessage);

        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        if (isCancellationMessage) {
            // Break so that we do not take in anymore input.
            // Let the sender request shutdown of the program
            // so that it can send the message first, unless enqueue failed.
            if (!isEnqueueSuccessful) {
                // If the enqueueing of the shutdown message failed, then
                // the threads won't be requested for shutdown by the
                // sender, because the sender won't get the message.
                // So the reader should should request shutdown regardless.
                requestShutdownOfAllThreadsForProgram();
            }
            break;
        }
    }
    return NULL;
}

/*
 * For the sender thread to get messages from queue. This will block the caller if
 * there are no messages on the queue.
 */
Message* KeyboardReader_getMessageFromQueue()
{
    if (s_outMessageQueue == NULL) {
        return NULL;
    }

    Message* pMessage = NULL;

    pthread_cleanup_push(unlockMutexesCleanup, &s_accessQueueMutex);
    pthread_mutex_lock(&s_accessQueueMutex);
    {
        while (List_count(s_outMessageQueue) == 0) {
            // When it blocks, the queue mutex will be unlocked so that the keyboard
            // reader can put messages on it and then signal to unblock the sender.
            pthread_cond_wait(&s_syncMessagesAvailableCondVar, &s_accessQueueMutex);
        }
        // Dequeue
        List_first(s_outMessageQueue);
        // Do not let us cancel this thread while it holds a pointer to a message
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        /* SENDER THREAD NOT CANCELLABLE HERE */
        pMessage = List_remove(s_outMessageQueue);
        if (pMessage == NULL || pMessage->pText == NULL) {
            // If the message isn't anything, enable cancels for this thread.
            // Note that pthread_cleanup_{push,pop} will make it so that the
            // mutex is unlocked when this thread cancels while it holds the mutex.
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        }
    }
    // This pop unlocks the mutex via unlockMutexesCleanup
    // It also will make sure the mutex is released if the thread
    // cancels while it is blocked by the cond_wait, since the
    // routine is triggered if the thread cancels.
    pthread_cleanup_pop(1);

    return pMessage;
}

void KeyboardReader_init()
{
    s_outMessageQueue = List_create();
    if (s_outMessageQueue != NULL) {
        int status = pthread_create(&s_threadPid, NULL, KeyboardReader_run, NULL);
        if (status != 0) {
            printf("Failed to create keyboard reader thread: %s\n", strerror(status));
            requestShutdownOfAllThreadsForProgram();
        }
    } else {
        fputs("Failed to create list for keyboard reader\n", stdout);
        requestShutdownOfAllThreadsForProgram();
    }
}

ShutdownStatus KeyboardReader_shutdown()
{
    pthread_cond_signal(&s_syncMessagesAvailableCondVar);
    pthread_mutex_unlock(&s_accessQueueMutex);

    return shutdownThreadWithPid(s_threadPid);
}

/*
 * Only called when all threads are shutdown.
 */
void KeyboardReader_destroyMutexAndCondAndFreeList()
{
    pthread_cond_destroy(&s_syncMessagesAvailableCondVar);
    pthread_mutex_destroy(&s_accessQueueMutex);

    List_free(s_outMessageQueue, freeMessageFn);
    s_outMessageQueue = NULL;
}
