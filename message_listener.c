#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <errno.h>

#include "common.h"
#include "message_listener.h"
#include "screen_printer.h"

static pthread_t s_threadPid;
static int s_socketDescriptor;
static in_port_t s_ourPort;

static void* Listener_run(void* stub)
{
    waitForAllThreadsReadyBarrier();

    s_socketDescriptor = getSocketFdOrCreateAndBindIfDoesntExist(s_ourPort);

    char messageRxBuffer[MSG_MAX_LEN];

    bool shouldExitProgram = false;
    while (1) {
        // Receive UDP packets
        struct sockaddr_in sinRemote;
        unsigned int sin_len = sizeof(sinRemote);
        memset(messageRxBuffer, 0, sizeof(char) * MSG_MAX_LEN);

        // Blocking call to receive data from UDP packets. No persistent connection required,
        // unlike TCP.
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        ssize_t bytesRx = recvfrom(s_socketDescriptor,
                               messageRxBuffer, MSG_MAX_LEN, 0,
                               (struct sockaddr*) &sinRemote, &sin_len);

        if (bytesRx == -1) {
            fputs("**Error receiving message**\n", stdout);
            requestShutdownOfAllThreadsForProgram();
            break;
        }
        // If there is an incoming pMessage, handle it before we do anything else.
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, false);
        /* LISTENER THREAD NOT CANCELABLE HERE */

        // Scan the input buffer for the termination line "!\n".
        // When it find
        // Note: This function doesn't rely on null termination to exit its loop, so
        // it's fine to put this check before the null termination placement.
        shouldExitProgram = checkAndDiscardRestIfMessageHasTerminationLine(messageRxBuffer, NULL);

        // Make it null terminated (so string functions work):
        ssize_t terminateIdx = (bytesRx < MSG_MAX_LEN) ? bytesRx : MSG_MAX_LEN - 1;
        messageRxBuffer[terminateIdx] = 0;

        // Copy the pMessageText so that we can add it to the print queue.
        // accounting for the needed \0 character.
        char* pMessageText = malloc(bytesRx + 1);
        memset(pMessageText, 0, bytesRx + 1);
        strncpy(pMessageText, messageRxBuffer, bytesRx);

        Message* pMessage = malloc(sizeof(Message));
        pMessage->pText = pMessageText;
        pMessage->isShutdownMessage = shouldExitProgram;

        // This potentially ignores the added pMessage if there are not enough list nodes.
        bool isEnqueueSuccessful = ScreenPrinter_putMessageOnQueue(pMessage);

        // Now that pMessage is put on the queue, it is safe to cancel the listener thread.
        /* LISTENER THREAD CANCELABLE HERE */
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        if (shouldExitProgram) {
            // Break so that we do not listen to anymore messages.
            // Let the screen printer request shutdown of the program
            // so that it can show the message first, unless enqueueing failed.
            if (!isEnqueueSuccessful) {
                // If the enqueueing of the shutdown message failed, then
                // the threads won't be requested for shutdown by the
                // printer because it won't get the message.
                requestShutdownOfAllThreadsForProgram();
            }
            break;
        }
    }
    return NULL;
}

void Listener_init(in_port_t ourPort)
{
    s_ourPort = ourPort;
    int status = pthread_create(&s_threadPid, NULL, Listener_run, NULL);
    if (status != 0) {
        printf("Failed to create listener thread: %s\n", strerror(status));
        requestShutdownOfAllThreadsForProgram();
    }
}

/*
 * Returns true if shutdown already.
 */
ShutdownStatus Listener_shutdown()
{
    return shutdownThreadWithPid(s_threadPid);
}
