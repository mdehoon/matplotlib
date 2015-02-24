#include <Python.h>
#include <X11/Intrinsic.h>

#include <tcl.h>
#include "events.h"

#if PY_MAJOR_VERSION >= 3
#define PY3K 1
#else
#define PY3K 0
#endif

extern int initialized;

extern void InitNotifier(void);

typedef struct FileHandler {
    int fd;
    int mask;                   /* Mask of desired events: TCL_READABLE,
                                 * etc. */
    int readyMask;              /* Events that have been seen since the last
                                 * time FileHandlerEventProc was called for
                                 * this file. */
    XtInputId read;             /* Xt read callback handle. */
    XtInputId write;            /* Xt write callback handle. */
    XtInputId except;           /* Xt exception callback handle. */
    void(*proc)(void*, int);    /* Procedure to call */
    void* clientData;           /* Argument to pass to proc. */
    PyObject* argument;
    struct FileHandler *nextPtr;/* Next in list of all files we care about. */
} FileHandler;

extern struct NotifierState {
    XtAppContext appContext;    /* The context used by the Xt notifier. */
    PyObject* currentTimeout;/* Handle of current timer. */
    FileHandler *firstFileHandlerPtr;
                                /* Pointer to head of file handler list. */
} notifier;

/*
 *----------------------------------------------------------------------
 *
 * WaitForEvent --
 *
 *      This function is called by Tcl_DoOneEvent to wait for new events on
 *      the message queue. If the block time is 0, then Tcl_WaitForEvent just
 *      polls without blocking.
 *
 * Results:
 *      Returns 1 if an event was found, else 0. This ensures that
 *      Tcl_DoOneEvent will return 1, even if the event is handled by non-Tcl
 *      code.
 *
 * Side effects:
 *      Queues file events that are detected by the select.
 *
 *----------------------------------------------------------------------
 */

static int
WaitForEvent(const Tcl_Time *timePtr)      /* Maximum block time, or NULL. */
{
    int timeout;
    if (!initialized) {
        InitNotifier();
    }
    if (timePtr) {
        timeout = timePtr->sec * 1000 + timePtr->usec / 1000;
        if (timeout == 0) {
            if (!PyEvents_HavePendingEvents()) {
                return 0;
            }
        } else {
            Tcl_SetTimer(timePtr);
        }
    }
    PyEvents_ProcessEvent();
    return 1;
}

static void TimerProc(PyObject* timer)
{
    if (timer != notifier.currentTimeout) {
        /* this is not supposed to happen */
        return;
    }
    Py_DECREF(notifier.currentTimeout);
    notifier.currentTimeout = NULL;
    Tcl_ServiceAll();
}

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
    unsigned long timeout;
    if (!initialized) {
        InitNotifier();
    }

    if (notifier.currentTimeout != 0) {
        PyEvents_RemoveTimer(notifier.currentTimeout);
        Py_DECREF(notifier.currentTimeout);
    }
    if (timePtr) {
        timeout = timePtr->sec * 1000 + timePtr->usec / 1000;
        notifier.currentTimeout = PyEvents_AddTimer(timeout, TimerProc);
    } else {
        notifier.currentTimeout = NULL;
    }
}

typedef struct {
    PyObject_HEAD
    ClientData clientData;
} FileHandlerDataObject;

static PyTypeObject FileHandlerDataType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "events_tkinter.FileHandlerData",  /*tp_name*/
    sizeof(FileHandlerDataObject),       /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    0,                         /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT,        /*tp_flags*/
    "FileHandlerData object",            /*tp_doc */
};

/*
 *----------------------------------------------------------------------
 *
 * CreateFileHandler --
 *
 *      This procedure registers a file handler with the Xt notifier.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates a new file handler structure and registers one or more input
 *      procedures with Xt.
 *
 *----------------------------------------------------------------------
 */

static void
CreateFileHandler(
    int fd,                     /* Handle of stream to watch. */
    int mask,                   /* OR'ed combination of TCL_READABLE,
                                 * TCL_WRITABLE, and TCL_EXCEPTION: indicates
                                 * conditions under which proc should be
                                 * called. */
    Tcl_FileProc *proc,         /* Procedure to call for each selected
                                 * event. */
    ClientData clientData)      /* Arbitrary data to pass to proc. */
{
    FileHandler* filePtr;
    PyObject* argument;
    FileHandlerDataObject* object;
    if (!initialized) {
        InitNotifier();
    }
    argument = PyType_GenericNew(&FileHandlerDataType, NULL, NULL);
    object = (FileHandlerDataObject*)argument;
    object->clientData = clientData;

    for (filePtr = notifier.firstFileHandlerPtr; filePtr != NULL;
            filePtr = filePtr->nextPtr) {
        if (filePtr->fd == fd) {
            break;
        }
    }
    if (filePtr == NULL) {
        filePtr = ckalloc(sizeof(FileHandler));
        filePtr->fd = fd;
        filePtr->read = 0;
        filePtr->write = 0;
        filePtr->except = 0;
        filePtr->readyMask = 0;
        filePtr->mask = 0;
        filePtr->nextPtr = notifier.firstFileHandlerPtr;
        notifier.firstFileHandlerPtr = filePtr;
    }
    else {
        Py_XDECREF(filePtr->argument);
    }
    filePtr->proc = proc;
    filePtr->clientData = object->clientData;
    filePtr->argument = argument;
    if (mask & TCL_READABLE) {
        if (!(filePtr->mask & TCL_READABLE)) {
            filePtr->read = PyEvents_CreateFileHandler(fd, mask, filePtr);
        }
    } else {
        if (filePtr->mask & TCL_READABLE) {
            XtRemoveInput(filePtr->read);
        }
    }
    if (mask & TCL_WRITABLE) {
        if (!(filePtr->mask & TCL_WRITABLE)) {
            filePtr->write = PyEvents_CreateFileHandler(fd, mask, filePtr);
        }
    } else {
        if (filePtr->mask & TCL_WRITABLE) {
            XtRemoveInput(filePtr->write);
        }
    }
    if (mask & TCL_EXCEPTION) {
        if (!(filePtr->mask & TCL_EXCEPTION)) {
            filePtr->except = PyEvents_CreateFileHandler(fd, mask, filePtr);
        }
    } else {
        if (filePtr->mask & TCL_EXCEPTION) {
            XtRemoveInput(filePtr->except);
        }
    }
    filePtr->mask = mask;
}

/*
 *----------------------------------------------------------------------
 *
 * DeleteFileHandler --
 *
 *      Cancel a previously-arranged callback arrangement for a file.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      If a callback was previously registered on file, remove it.
 *
 *----------------------------------------------------------------------
 */

static void
DeleteFileHandler(int fd)       /* Stream id for which to remove callback
                                 * procedure. */
{
    FileHandler *prevPtr;
    FileHandler* filePtr = NULL;
    if (!initialized) {
        InitNotifier();
    }

    /*
     * Find the entry for the given file (and return if there isn't one).
     */

    for (prevPtr = NULL, filePtr = notifier.firstFileHandlerPtr; ;
            prevPtr = filePtr, filePtr = filePtr->nextPtr) {
        if (filePtr == NULL) {
            return;
        }
        if (filePtr->fd == fd) {
            break;
        }
    }
    if (prevPtr == NULL) {
        notifier.firstFileHandlerPtr = filePtr->nextPtr;
    } else {
        prevPtr->nextPtr = filePtr->nextPtr;
    }

    /*
     * Clean up information in the callback record.
     */

    if (filePtr->mask & TCL_READABLE) {
        PyEvents_DeleteFileHandler(filePtr->read);
    }
    if (filePtr->mask & TCL_WRITABLE) {
        PyEvents_DeleteFileHandler(filePtr->write);
    }
    if (filePtr->mask & TCL_EXCEPTION) {
        PyEvents_DeleteFileHandler(filePtr->except);
    }

    Py_XDECREF(filePtr->argument);
    ckfree(filePtr);
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
    if (PyType_Ready(&FileHandlerDataType) < 0)
        goto error;
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
error:
#if PY3K
    return NULL;
#else
    return;
#endif
}
