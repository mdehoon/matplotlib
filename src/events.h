#ifndef Py_EVENTSMODULE_H
#define Py_EVENTSMODULE_H

/* Header file for events */

/* C API functions */
#define PyEvents_AddTimer_NUM 0
#define PyEvents_AddTimer_RETURN PyObject*
#define PyEvents_AddTimer_PROTO (unsigned long timeout, void(*callback)(PyObject*))
#define PyEvents_RemoveTimer_NUM 1
#define PyEvents_RemoveTimer_RETURN void
#define PyEvents_RemoveTimer_PROTO (PyObject* timer)
#define PyEvents_ProcessEvent_NUM 2
#define PyEvents_ProcessEvent_RETURN void
#define PyEvents_ProcessEvent_PROTO (void)
#define PyEvents_HavePendingEvents_NUM 3
#define PyEvents_HavePendingEvents_RETURN int
#define PyEvents_HavePendingEvents_PROTO (void)
#define PyEvents_CreateFileHandler_NUM 4
#define PyEvents_CreateFileHandler_RETURN void
#define PyEvents_CreateFileHandler_PROTO (int fd, int mask, void(*proc)(void*, int), void* data)
#define PyEvents_DeleteFileHandler_NUM 5
#define PyEvents_DeleteFileHandler_RETURN void
#define PyEvents_DeleteFileHandler_PROTO (int fd)

/* Total number of C API pointers */
#define PyEvents_API_pointers 6


#ifdef EVENTS_MODULE
/* This section is used when compiling events.c */

static PyEvents_AddTimer_RETURN PyEvents_AddTimer PyEvents_AddTimer_PROTO;
static PyEvents_RemoveTimer_RETURN PyEvents_RemoveTimer PyEvents_RemoveTimer_PROTO;
static PyEvents_ProcessEvent_RETURN PyEvents_ProcessEvent PyEvents_ProcessEvent_PROTO;
static PyEvents_HavePendingEvents_RETURN PyEvents_HavePendingEvents PyEvents_HavePendingEvents_PROTO;
static PyEvents_CreateFileHandler_RETURN PyEvents_CreateFileHandler PyEvents_CreateFileHandler_PROTO;
static PyEvents_DeleteFileHandler_RETURN PyEvents_DeleteFileHandler PyEvents_DeleteFileHandler_PROTO;

#else
/* This section is used in modules that use the API */

static void **PyEvents_API;

#define PyEvents_System \
 (*(PyEvents_System_RETURN (*)PyEvents_System_PROTO) PyEvents_API[PyEvents_System_NUM])
#define PyEvents_AddTimer \
 (*(PyEvents_AddTimer_RETURN (*)PyEvents_AddTimer_PROTO) PyEvents_API[PyEvents_AddTimer_NUM])
#define PyEvents_RemoveTimer \
 (*(PyEvents_RemoveTimer_RETURN (*)PyEvents_RemoveTimer_PROTO) PyEvents_API[PyEvents_RemoveTimer_NUM])
#define PyEvents_ProcessEvent \
 (*(PyEvents_ProcessEvent_RETURN (*)PyEvents_ProcessEvent_PROTO) PyEvents_API[PyEvents_ProcessEvent_NUM])
#define PyEvents_HavePendingEvents \
 (*(PyEvents_HavePendingEvents_RETURN (*)PyEvents_HavePendingEvents_PROTO) PyEvents_API[PyEvents_HavePendingEvents_NUM])
#define PyEvents_CreateFileHandler \
 (*(PyEvents_CreateFileHandler_RETURN (*)PyEvents_CreateFileHandler_PROTO) PyEvents_API[PyEvents_CreateFileHandler_NUM])
#define PyEvents_DeleteFileHandler \
 (*(PyEvents_DeleteFileHandler_RETURN (*)PyEvents_DeleteFileHandler_PROTO) PyEvents_API[PyEvents_DeleteFileHandler_NUM])

/* Return -1 on error, 0 on success.
 * PyCapsule_Import will set an exception if there's an error.
 */
static int
import_events(void)
{
    PyEvents_API = (void **)PyCapsule_Import("events._C_API", 0);
    return (PyEvents_API != NULL) ? 0 : -1;
}

#endif

#endif
