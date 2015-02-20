#ifndef Py_EVENTSMODULE_H
#define Py_EVENTSMODULE_H

/* Header file for events */

/* C API functions */
#define PyEvents_System_NUM 0
#define PyEvents_System_RETURN int
#define PyEvents_System_PROTO (const char *command)
#define PyEvents_AddTimer_NUM 1
#define PyEvents_AddTimer_RETURN XtIntervalId
#define PyEvents_AddTimer_PROTO (unsigned long timeout)
#define PyEvents_RemoveTimer_NUM 2
#define PyEvents_RemoveTimer_RETURN void
#define PyEvents_RemoveTimer_PROTO (XtIntervalId timer)

/* Total number of C API pointers */
#define PyEvents_API_pointers 3


#ifdef EVENTS_MODULE
/* This section is used when compiling events.c */

static PyEvents_System_RETURN PyEvents_System PyEvents_System_PROTO;
static PyEvents_AddTimer_RETURN PyEvents_AddTimer PyEvents_AddTimer_PROTO;
static PyEvents_RemoveTimer_RETURN PyEvents_RemoveTimer PyEvents_RemoveTimer_PROTO;

#else
/* This section is used in modules that use the API */

static void **PyEvents_API;

#define PyEvents_System \
 (*(PyEvents_System_RETURN (*)PyEvents_System_PROTO) PyEvents_API[PyEvents_System_NUM])
#define PyEvents_AddTimer \
 (*(PyEvents_AddTimer_RETURN (*)PyEvents_AddTimer_PROTO) PyEvents_API[PyEvents_AddTimer_NUM])
#define PyEvents_RemoveTimer \
 (*(PyEvents_RemoveTimer_RETURN (*)PyEvents_RemoveTimer_PROTO) PyEvents_API[PyEvents_RemoveTimer_NUM])

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
