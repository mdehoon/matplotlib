#ifndef Py_EVENTSMODULE_H
#define Py_EVENTSMODULE_H

/* Header file for events */

#define PyEvents_READABLE 2
#define PyEvents_WRITABLE 4
#define PyEvents_EXCEPTION 8

/* C API functions */
#define PyEvents_WaitForEvent_NUM 0
#define PyEvents_WaitForEvent_RETURN int
#define PyEvents_WaitForEvent_PROTO (int)
#define PyEvents_CreateSocket_NUM 1
#define PyEvents_CreateSocket_RETURN PyObject*
#define PyEvents_CreateSocket_PROTO (int fd, int mask, void(*proc)(void* info, int mask), void* argument)
#define PyEvents_DeleteSocket_NUM 2
#define PyEvents_DeleteSocket_RETURN void
#define PyEvents_DeleteSocket_PROTO (PyObject* socket)

/* Total number of C API pointers */
#define PyEvents_API_pointers 3


#ifdef EVENTS_MODULE
/* This section is used when compiling events.c */

static PyEvents_WaitForEvent_RETURN PyEvents_WaitForEvent PyEvents_WaitForEvent_PROTO;
static PyEvents_CreateSocket_RETURN PyEvents_CreateSocket PyEvents_CreateSocket_PROTO;
static PyEvents_DeleteSocket_RETURN PyEvents_DeleteSocket PyEvents_DeleteSocket_PROTO;

#else
/* This section is used in modules that use the API */

static void **PyEvents_API;

#define PyEvents_WaitForEvent \
 (*(PyEvents_WaitForEvent_RETURN (*)PyEvents_WaitForEvent_PROTO) PyEvents_API[PyEvents_WaitForEvent_NUM])
#define PyEvents_CreateSocket \
 (*(PyEvents_CreateSocket_RETURN (*)PyEvents_CreateSocket_PROTO) PyEvents_API[PyEvents_CreateSocket_NUM])
#define PyEvents_DeleteSocket \
 (*(PyEvents_DeleteSocket_RETURN (*)PyEvents_DeleteSocket_PROTO) PyEvents_API[PyEvents_DeleteSocket_NUM])

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
