#ifndef Py_EVENTSMODULE_H
#define Py_EVENTSMODULE_H

/* Header file for events */

typedef void (*Observer)(void);

#define PyEvents_READABLE 2
#define PyEvents_WRITABLE 4
#define PyEvents_EXCEPTION 8

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
#define PyEvents_WaitForEvent_NUM 4
#define PyEvents_WaitForEvent_RETURN int
#define PyEvents_WaitForEvent_PROTO (int)
#define PyEvents_CreateSocket_NUM 5
#define PyEvents_CreateSocket_RETURN PyObject*
#define PyEvents_CreateSocket_PROTO (int fd, int mask, void(*proc)(void* info, int mask), void* argument)
#define PyEvents_DeleteSocket_NUM 6
#define PyEvents_DeleteSocket_RETURN void
#define PyEvents_DeleteSocket_PROTO (PyObject* socket)
#define PyEvents_AddObserver_NUM 7
#define PyEvents_AddObserver_RETURN void
#define PyEvents_AddObserver_PROTO (int activity, Observer observer)
#define PyEvents_RemoveObserver_NUM 8
#define PyEvents_RemoveObserver_RETURN void
#define PyEvents_RemoveObserver_PROTO (int activity, Observer observer)

/* Total number of C API pointers */
#define PyEvents_API_pointers 9


#ifdef EVENTS_MODULE
/* This section is used when compiling events.c */

static PyEvents_AddTimer_RETURN PyEvents_AddTimer PyEvents_AddTimer_PROTO;
static PyEvents_RemoveTimer_RETURN PyEvents_RemoveTimer PyEvents_RemoveTimer_PROTO;
static PyEvents_ProcessEvent_RETURN PyEvents_ProcessEvent PyEvents_ProcessEvent_PROTO;
static PyEvents_HavePendingEvents_RETURN PyEvents_HavePendingEvents PyEvents_HavePendingEvents_PROTO;
static PyEvents_WaitForEvent_RETURN PyEvents_WaitForEvent PyEvents_WaitForEvent_PROTO;
static PyEvents_CreateSocket_RETURN PyEvents_CreateSocket PyEvents_CreateSocket_PROTO;
static PyEvents_DeleteSocket_RETURN PyEvents_DeleteSocket PyEvents_DeleteSocket_PROTO;
static PyEvents_AddObserver_RETURN PyEvents_AddObserver PyEvents_AddObserver_PROTO;
static PyEvents_RemoveObserver_RETURN PyEvents_RemoveObserver PyEvents_RemoveObserver_PROTO;

#else
/* This section is used in modules that use the API */

static void **PyEvents_API;

#define PyEvents_AddTimer \
 (*(PyEvents_AddTimer_RETURN (*)PyEvents_AddTimer_PROTO) PyEvents_API[PyEvents_AddTimer_NUM])
#define PyEvents_RemoveTimer \
 (*(PyEvents_RemoveTimer_RETURN (*)PyEvents_RemoveTimer_PROTO) PyEvents_API[PyEvents_RemoveTimer_NUM])
#define PyEvents_ProcessEvent \
 (*(PyEvents_ProcessEvent_RETURN (*)PyEvents_ProcessEvent_PROTO) PyEvents_API[PyEvents_ProcessEvent_NUM])
#define PyEvents_HavePendingEvents \
 (*(PyEvents_HavePendingEvents_RETURN (*)PyEvents_HavePendingEvents_PROTO) PyEvents_API[PyEvents_HavePendingEvents_NUM])
#define PyEvents_WaitForEvent \
 (*(PyEvents_WaitForEvent_RETURN (*)PyEvents_WaitForEvent_PROTO) PyEvents_API[PyEvents_WaitForEvent_NUM])
#define PyEvents_CreateSocket \
 (*(PyEvents_CreateSocket_RETURN (*)PyEvents_CreateSocket_PROTO) PyEvents_API[PyEvents_CreateSocket_NUM])
#define PyEvents_DeleteSocket \
 (*(PyEvents_DeleteSocket_RETURN (*)PyEvents_DeleteSocket_PROTO) PyEvents_API[PyEvents_DeleteSocket_NUM])
#define PyEvents_AddObserver \
 (*(PyEvents_AddObserver_RETURN (*)PyEvents_AddObserver_PROTO) PyEvents_API[PyEvents_AddObserver_NUM])
#define PyEvents_RemoveObserver \
 (*(PyEvents_RemoveObserver_RETURN (*)PyEvents_RemoveObserver_PROTO) PyEvents_API[PyEvents_RemoveObserver_NUM])

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
