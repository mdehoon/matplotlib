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

/* Total number of C API pointers */
#define PyEvents_API_pointers 1


#ifdef EVENTS_MODULE
/* This section is used when compiling events.c */

static PyEvents_WaitForEvent_RETURN PyEvents_WaitForEvent PyEvents_WaitForEvent_PROTO;

#else
/* This section is used in modules that use the API */

static void **PyEvents_API;

#define PyEvents_WaitForEvent \
 (*(PyEvents_WaitForEvent_RETURN (*)PyEvents_WaitForEvent_PROTO) PyEvents_API[PyEvents_WaitForEvent_NUM])

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
