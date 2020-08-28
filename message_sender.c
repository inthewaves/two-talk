#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <assert.h>
#include <errno.h>

#include "common.h"
#include "message_sender.h"
#include "keyboard_reader.h"

static pthread_t s_threadPid;
static in_addr_t s_destinationAddr;
static in_port_t s_destinationPort;
static in_port_t s_ourPort;

static int s_socketDescriptor;

static void* Sender_run(void* stub)
{
    waitForAllThreadsReadyBarrier();

    // Get the binded socket for UDP
    s_socketDescriptor = getSocketFdOrCreateAndBindIfDoesntExist(s_ourPort);

    // Set up the Internet socket address to communicate to the other two-chat client.
    struct sockaddr_in sinRemote;
    memset(&sinRemote, 0, sizeof(sinRemote));
    sinRemote.sin_family = AF_INET;
    sinRemote.sin_port = htons(s_destinationPort);
    sinRemote.sin_addr.s_addr = htonl(s_destinationAddr);

    Message* pOutputMessage = NULL;
    char messageTxBuffer[MSG_MAX_LEN];

    bool shouldExitProgram = false;
    unsigned int sin_len;
    while (1) {
        // If we don't zero the buffer, then subsequent messages may still have pieces
        // from the previous message.
        memset(messageTxBuffer, 0, sizeof(char) * MSG_MAX_LEN);

        // Get the reply message and prepare to send it
        // This call will block if there are no messages yet in the queue.
        // The queue is managed by the keyboard reader.
        pOutputMessage = KeyboardReader_getMessageFromQueue();
        /* SENDER THREAD NOT CANCELLABLE HERE */
        // Sender will not be cancelled here to make sure we don't leak pMessage and to make
        // sure we get this message out first. The socket will not be killed until all threads
        // are shutdown.

        if (pOutputMessage == NULL || pOutputMessage->pText == NULL) {
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
            requestShutdownOfAllThreadsForProgram();
            break;
        }

        strncpy(messageTxBuffer, pOutputMessage->pText, MSG_MAX_LEN);
        shouldExitProgram = pOutputMessage->isShutdownMessage;
        freeMessageFn(pOutputMessage);

        size_t sizeOfMessage = strnlen(messageTxBuffer, MSG_MAX_LEN);

        sin_len = sizeof(sinRemote);
        // Transmit the message:
        int status = sendto(s_socketDescriptor, messageTxBuffer, sizeOfMessage, 0,
                            (struct sockaddr*) &sinRemote, sin_len);
        if (status == -1) {
            fputs("**Error sending message**\n", stdout);
        }

        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        /* SENDER THREAD CANCELLABLE HERE */

        if (shouldExitProgram) {
            // This should be the last thing we send, so now we can
            // request shutdown.
            requestShutdownOfAllThreadsForProgram();
            break;
        }
    }

    return NULL;
}

void Sender_init(in_addr_t destinationAddr, in_port_t ourPort,
                 in_port_t destinationPort)
{
    s_destinationAddr = destinationAddr;
    s_ourPort = ourPort;
    s_destinationPort = destinationPort;

    int status = pthread_create(&s_threadPid, NULL, Sender_run, NULL);
    if (status != 0) {
        printf("Failed to create sender thread: %s\n", strerror(status));
        requestShutdownOfAllThreadsForProgram();
    }
}

ShutdownStatus Sender_shutdown()
{
    return shutdownThreadWithPid(s_threadPid);
}
