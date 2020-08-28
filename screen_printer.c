#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <asm/errno.h>
#include "screen_printer.h"
#include "keyboard_reader.h"
#include "list.h"
#include "common.h"

static pthread_t s_threadPid;
static pthread_cond_t s_syncMessagesAvailableCondVar = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t s_accessQueueMutex = PTHREAD_MUTEX_INITIALIZER;

static List* s_pInMessageQueue = NULL;

static Message* getMessageFromQueue()
{
    if (s_pInMessageQueue == NULL) {
        return NULL;
    }

    // Example used for pthread_cleanup_push:
    // https://www.systutorials.com/docs/linux/man/3p-pthread_cleanup_push/
    Message* pMessage = NULL;

    pthread_cleanup_push(unlockMutexesCleanup, &s_accessQueueMutex);
    pthread_mutex_lock(&s_accessQueueMutex);
    {
        // Block if list is empty until list has items.
        while (List_count(s_pInMessageQueue) == 0) {
            // When it blocks, the mutex will be released to allow the listener process
            // to be able to add messages onto the queue.
            pthread_cond_wait(&s_syncMessagesAvailableCondVar, &s_accessQueueMutex);
        }
        // Dequeue
        List_first(s_pInMessageQueue);
        // Do not let the printer thread be cancelled until pMessage is freed or it is NULL.
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        /* LISTENER THREAD NOT CANCELABLE HERE */
        pMessage = List_remove(s_pInMessageQueue);
        if (pMessage == NULL || pMessage->pText == NULL) {
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        }
    }
    // This pop releases the mutex via unlockMutexesCleanup
    // It also will make sure the mutex is released if the thread
    // cancels while it is blocked by the cond_wait, since the
    // routine is triggered if the thread cancels.
    pthread_cleanup_pop(1);

    return pMessage;
}

static void* ScreenPrinter_run(void* stub)
{
    waitForAllThreadsReadyBarrier();
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    while (1) {
        // This blocks until there is a pMessage on list.
        // This call will set the cancel state for the printer thread to be
        // disabled so that it does not cancel while it hasn't freed pMessage.
        Message* pMessage = getMessageFromQueue();
        /* PRINTER THREAD NOT CANCELABLE HERE */

        if (pMessage == NULL || pMessage->pText == NULL) {
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            continue;
        }

        bool shouldExitProgram = pMessage->isShutdownMessage;
        fputs(pMessage->pText, stdout);
        freeMessageFn(pMessage);

        // Now that we have displayed and freed our pMessage, we can now set the printer thread
        // as ready to cancel.
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        /* PRINTER THREAD CANCELABLE HERE */
        fflush(stdout);

        if (shouldExitProgram) {
            requestShutdownOfAllThreadsForProgram();
            // Stop printing out output if we are shutting down.
            break;
        }
    }
    return NULL;
}

/*
 * For the message listener to add messages on the queue.
 * Returns true if it was successfully put onto the queue, and false if not.
 */
bool ScreenPrinter_putMessageOnQueue(Message* pMessage)
{
    bool isEnqueueSuccessful;

    pthread_cleanup_push(unlockMutexesCleanup, &s_accessQueueMutex);
    pthread_mutex_lock(&s_accessQueueMutex);
    {
        if (List_append(s_pInMessageQueue, pMessage) == LIST_FAIL) {
            fputs("**The receiving message queue is too large!**\n", stdout);
            fputs("**The most recent message will be dropped, please tell the other to resend**\n",
                  stdout);
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            freeMessageFn(pMessage);
            isEnqueueSuccessful = false;
        } else {
            pthread_cond_signal(&s_syncMessagesAvailableCondVar);
            isEnqueueSuccessful = true;
        }
    }
    // Unlocks the mutex.
    pthread_cleanup_pop(1);

    return isEnqueueSuccessful;
}

void ScreenPrinter_init()
{
    s_pInMessageQueue = List_create();
    if (s_pInMessageQueue != NULL) {
        int status = pthread_create(&s_threadPid, NULL, ScreenPrinter_run, NULL);
        if (status != 0) {
            printf("Failed to create screen display thread: %s\n", strerror(status));
            requestShutdownOfAllThreadsForProgram();
        }
    } else {
        fputs("Failed to create list for screen display\n", stdout);
        requestShutdownOfAllThreadsForProgram();
    }
}

ShutdownStatus ScreenPrinter_shutdown()
{
    pthread_cond_signal(&s_syncMessagesAvailableCondVar);
    pthread_mutex_unlock(&s_accessQueueMutex);

    return shutdownThreadWithPid(s_threadPid);
}

/**
 * This is only run when all of the threads are shutdown.
 */
void ScreenPrinter_destroyMutexAndCondAndFreeLists()
{
    pthread_cond_destroy(&s_syncMessagesAvailableCondVar);
    pthread_mutex_destroy(&s_accessQueueMutex);

    List_free(s_pInMessageQueue, freeMessageFn);
    s_pInMessageQueue = NULL;
}
