#ifndef _SCREEN_PRINTER_H
#define _SCREEN_PRINTER_H

#include "common.h"

void ScreenPrinter_init();

/*
 * For the message listener to add messages on the queue.
 * Returns true if it was successfully put onto the queue, and false if not.
 */
bool ScreenPrinter_putMessageOnQueue(Message* pMessage);

ShutdownStatus ScreenPrinter_shutdown();

void ScreenPrinter_destroyMutexAndCondAndFreeLists();

#endif // _SCREEN_PRINTER_H
