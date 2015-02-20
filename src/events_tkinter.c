#include <Python.h>
#include <X11/Intrinsic.h>

#include <tcl.h>
#include "events.h"

#if PY_MAJOR_VERSION >= 3
#define PY3K 1
#else
#define PY3K 0
#endif

extern void             CreateFileHandler(int fd, int mask,
                            Tcl_FileProc *proc, ClientData clientData);
extern void             DeleteFileHandler(int fd);
extern int              WaitForEvent(const Tcl_Time * timePtr);

extern int initialized;

extern void InitNotifier(void);

extern struct NotifierState {
    XtAppContext appContext;    /* The context used by the Xt notifier. */
    XtIntervalId currentTimeout;/* Handle of current timer. */
    void* *firstFileHandlerPtr;
                                /* Pointer to head of file handler list. */
} notifier;




/*
 *----------------------------------------------------------------------
 *
 * SetTimer --
 *
 *      This procedure sets the current notifier timeout value.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Replaces any previous timer.
 *
 *----------------------------------------------------------------------
 */

static void
SetTimer(
    const Tcl_Time *timePtr)            /* Timeout value, may be NULL. */
{
    long timeout;
    if (!initialized) {
        InitNotifier();
    }

    if (notifier.currentTimeout != 0) {
        PyEvents_RemoveTimer(notifier.currentTimeout);
    }
    if (timePtr) {
        timeout = timePtr->sec * 1000 + timePtr->usec / 1000;
        notifier.currentTimeout = PyEvents_AddTimer((unsigned long) timeout);
    } else {
        notifier.currentTimeout = 0;
    }
}

static PyObject *
events_system(PyObject *self, PyObject *args)
{
    const char *command;
    int sts;

    if (!PyArg_ParseTuple(args, "s", &command))
        return NULL;
    sts = PyEvents_System(command);
    return Py_BuildValue("i", sts);
}

static PyObject*
load(PyObject* unused)
{
    Tcl_NotifierProcs np;
    memset(&np, 0, sizeof(np));
    np.createFileHandlerProc = CreateFileHandler;
    np.deleteFileHandlerProc = DeleteFileHandler;
    np.setTimerProc = SetTimer;
    np.waitForEventProc = WaitForEvent;
    Tcl_SetNotifier(&np);
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject*
unload(PyObject* unused)
{
    Tcl_NotifierProcs np;
    memset(&np, 0, sizeof(np));
    Tcl_SetNotifier(&np);
    Py_INCREF(Py_None);
    return Py_None;
}

static struct PyMethodDef methods[] = {
   {"system",
    (PyCFunction)events_system,
    METH_VARARGS,
    "system call"
   },
   {"load",
    (PyCFunction)load,
    METH_NOARGS,
    "adds the Tcl/Tk notifier from the event loop"
   },
   {"unload",
    (PyCFunction)unload,
    METH_NOARGS,
    "removes the Tcl/Tk notifier from the event loop"
   },
   {NULL,          NULL, 0, NULL} /* sentinel */
};

#if PY3K
static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "events_tkinter",
    "events_tkinter module",
    -1,
    methods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyObject* PyInit_events_tkinter(void)

#else

void initevents_tkinter(void)
#endif
{
    PyObject *module;
#if PY3K
    module = PyModule_Create(&moduledef);
    if (module==NULL) return NULL;
#else
    module = Py_InitModule4("events_tkinter",
                            methods,
                            "events_tkinter module",
                            NULL,
                            PYTHON_API_VERSION);
#endif
    if (import_events() < 0)
#if PY3K
        return NULL;
#else
        return;
#endif
    
#if PY3K
    return module;
#endif
}
