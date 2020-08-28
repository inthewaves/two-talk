#ifndef _KEYBOARD_READER_H
#define _KEYBOARD_READER_H

#include "common.h"

void KeyboardReader_init();

/*
 * For the sender thread to get messages from queue. This will block the caller if
 * there are no messages on the queue.
 */
Message* KeyboardReader_getMessageFromQueue();

ShutdownStatus KeyboardReader_shutdown();

void KeyboardReader_destroyMutexAndCondAndFreeList();

#endif // _KEYBOARD_READER_H
