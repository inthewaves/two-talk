#ifndef _MESSAGE_SENDER_H
#define _MESSAGE_SENDER_H

void Sender_init(in_addr_t destinationAddr, in_port_t ourPort,
                 in_port_t destinationPort);

ShutdownStatus Sender_shutdown();

#endif //_MESSAGE_SENDER_H
