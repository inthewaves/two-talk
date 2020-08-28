#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>

#include "keyboard_reader.h"
#include "screen_printer.h"
#include "message_sender.h"
#include "message_listener.h"
#include "common.h"

void printUsage()
{
    fputs("usage: ./two-chat <our port number> <remote machine name> <remote port number>\n", stdout);
}

/*
 * Returns the address of `hostname` as a in_addr_t (unsigned 32-bit long)
 * using the host's endianess (i.e. ntohl). The hostname can be either
 * alphanumeric (and will resolve it) or in x.x.x.x notation.
 */
in_addr_t getAddressOfHostnameAsHostLong(const char* hostname)
{
    struct addrinfo* pAddressList;

    // Setup a UDP IPv4 call.
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    // There's a memory leak caused by this function call...
    int statusCode = getaddrinfo(hostname, NULL, &hints, &pAddressList);
    if (statusCode != 0) {
        // Print out a detailed error
        fprintf(stderr, "Error in getting address of remote machine: %s\n",
                gai_strerror(statusCode));

        // There is a memory leak here, but if we try to do freeaddrinfo(pAddressList) here, we will
        // get
        //      munmap_chunk(): invalid pointer
        //      Aborted (core dumped)
        return 0;
    }

    if (pAddressList == NULL) {
        return 0;
    }
    // We assume that the first entry in the linked list is the right address.
    struct sockaddr_in* pHostAddr = (struct sockaddr_in*) pAddressList->ai_addr;
    in_addr_t returnAddress = ntohl(pHostAddr->sin_addr.s_addr);

    freeaddrinfo(pAddressList);
    return returnAddress;
}

int main(int argCount, char** args)
{
    if (argCount != 4) {
        printUsage();
        return 1;
    }

    in_addr_t addrOfRemote = getAddressOfHostnameAsHostLong(args[2]);
    if (addrOfRemote == 0) {
        fputs("Failed to get address! Exiting two-chat.\n", stdout);
        return 1;
    }

    errno = 0;
    in_port_t ourPort = strtol(args[1], NULL, 10);
    if (errno == ERANGE || errno == EINVAL) {
        fputs("Our port number is out of range. Please enter a valid port number.\n", stdout);
        fputs("Exiting two-chat.\n", stdout);
        return 1;
    }
    errno = 0;
    in_port_t destinationPort = strtol(args[3], NULL, 10);
    if (errno == ERANGE || errno == EINVAL) {
        fputs("Remote port number is out of range. Please enter a valid port number.\n", stdout);
        fputs("Exiting two-chat.\n", stdout);
        return 1;
    }

    // This prints its own error messages.
    if (getSocketFdOrCreateAndBindIfDoesntExist(ourPort) == -1) {
        fputs("Exiting two-chat.\n", stdout);
        return 1;
    }

    printf("----------------------------------------\n");
    printf("two-chat session started\n");
    printf("Our port: %d\n", ourPort);
    printf("Remote hostname: %s\n", args[2]);
    printf("Remote port: %d\n", destinationPort);
    printf("----------------------------------------\n");

    initBarriers();

    // Initialize the keyboard and screen printer first so that their queues can
    // be created.
    KeyboardReader_init();
    ScreenPrinter_init();
    Sender_init(addrOfRemote, ourPort, destinationPort);
    Listener_init(ourPort);

    waitForShutdownOfAllThreads();

    printf("----------------------------------------\n");
    fputs("Shutdown is complete.\n", stdout);
    fputs("Exiting two-chat.\n", stdout);
    printf("----------------------------------------\n");

    return 0;
}
