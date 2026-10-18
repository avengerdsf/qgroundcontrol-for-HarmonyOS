#pragma once

#include <pthread.h>

// HarmonyOS libc does not provide pthread_setcanceltype.
#undef PTHREAD_CANCEL_ASYNCHRONOUS
