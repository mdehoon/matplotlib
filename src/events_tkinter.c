#include <Python.h>
#include <X11/Intrinsic.h>

#include <tcl.h>
#include "events.h"

#if PY_MAJOR_VERSION >= 3
#define PY3K 1
#else
#define PY3K 0
#endif

extern int TclInExit(void); /* private function? */

/*
 * The following static indicates whether this module has been initialized.
 */

static int initialized = 0;

/*
 * This structure is used to keep track of the notifier info for a a
 * registered file.
 */

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

/*
 * The following structure is what is added to the Tcl event queue when file
 * handlers are ready to fire.
 */

typedef struct FileHandlerEvent {
    Tcl_Event header;           /* Information that is standard for all
                                 * events. */
    int fd;                     /* File descriptor that is ready. Used to find
                                 * the FileHandler structure for the file
                                 * (can't point directly to the FileHandler
                                 * structure because it could go away while
                                 * the event is queued). */
} FileHandlerEvent;

/*
 * The following static structure contains the state information for the Xt
 * based implementation of the Tcl notifier.
 */

static struct NotifierState {
    PyObject* currentTimeout;/* Handle of current timer. */
    FileHandler *firstFileHandlerPtr;
                                /* Pointer to head of file handler list. */
    int mode;                   /* Tcl's service mode */
} notifier;

/*
 *----------------------------------------------------------------------
 *
 * NotifierExitHandler --
 *
 *      This function is called to cleanup the notifier state before Tcl is
 *      unloaded.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Destroys the notifier window.
 *
 *----------------------------------------------------------------------
 */

static void
NotifierExitHandler(
    ClientData clientData)      /* Not used. */
{
    if (notifier.currentTimeout != 0) {
        PyEvents_RemoveTimer(notifier.currentTimeout);
        Py_DECREF(notifier.currentTimeout);
    }
    for (; notifier.firstFileHandlerPtr != NULL; ) {
        Tcl_DeleteFileHandler(notifier.firstFileHandlerPtr->fd);
    }
    initialized = 0;
}

static void set_service_mode(void)
{
    notifier.mode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
}

static void restore_service_mode(void)
{
    Tcl_SetServiceMode(notifier.mode);
}

/*
 *----------------------------------------------------------------------
 *
 * InitNotifier --
 *
 *	Initializes the notifier state.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a new exit handler.
 *
 *----------------------------------------------------------------------
 */

void
InitNotifier(void)
{
    /*
     * Only reinitialize if we are not in exit handling. The notifier can get
     * reinitialized after its own exit handler has run, because of exit
     * handlers for the I/O and timer sub-systems (order dependency).
     */

    if (TclInExit()) {
        return;
    }

    initialized = 1;
    Tcl_CreateExitHandler(NotifierExitHandler, NULL);
    PyEvents_AddObserver(0, set_service_mode);
    PyEvents_AddObserver(1, restore_service_mode);
}

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
 * FileHandlerEventProc --
 *
 *	This procedure is called by Tcl_ServiceEvent when a file event reaches
 *	the front of the event queue. This procedure is responsible for
 *	actually handling the event by invoking the callback for the file
 *	handler.
 *
 * Results:
 *	Returns 1 if the event was handled, meaning it should be removed from
 *	the queue. Returns 0 if the event was not handled, meaning it should
 *	stay on the queue. The only time the event isn't handled is if the
 *	TCL_FILE_EVENTS flag bit isn't set.
 *
 * Side effects:
 *	Whatever the file handler's callback procedure does.
 *
 *----------------------------------------------------------------------
 */

static int
FileHandlerEventProc(
    Tcl_Event *evPtr,		/* Event to service. */
    int flags)			/* Flags that indicate what events to handle,
				 * such as TCL_FILE_EVENTS. */
{
    FileHandler *filePtr;
    FileHandlerEvent *fileEvPtr = (FileHandlerEvent *) evPtr;
    int mask;

    if (!(flags & TCL_FILE_EVENTS)) {
	return 0;
    }

    /*
     * Search through the file handlers to find the one whose handle matches
     * the event. We do this rather than keeping a pointer to the file handler
     * directly in the event, so that the handler can be deleted while the
     * event is queued without leaving a dangling pointer.
     */

    for (filePtr = notifier.firstFileHandlerPtr; filePtr != NULL;
	    filePtr = filePtr->nextPtr) {
	if (filePtr->fd != fileEvPtr->fd) {
	    continue;
	}

	/*
	 * The code is tricky for two reasons:
	 * 1. The file handler's desired events could have changed since the
	 *    time when the event was queued, so AND the ready mask with the
	 *    desired mask.
	 * 2. The file could have been closed and re-opened since the time
	 *    when the event was queued. This is why the ready mask is stored
	 *    in the file handler rather than the queued event: it will be
	 *    zeroed when a new file handler is created for the newly opened
	 *    file.
	 */

	mask = filePtr->readyMask & filePtr->mask;
	filePtr->readyMask = 0;
	if (mask != 0) {
	    filePtr->proc(filePtr->clientData, mask);
	}
	break;
    }
    return 1;
}


/*
 *----------------------------------------------------------------------
 *
 * FileProc --
 *
 *	These procedures are called by Xt when a file becomes readable,
 *	writable, or has an exception.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Makes an entry on the Tcl event queue if the event is interesting.
 *
 *----------------------------------------------------------------------
 */

void
TclFileProc(
    XtPointer clientData,
    int mask)
{
    FileHandler *filePtr = (FileHandler *)clientData;
    FileHandlerEvent *fileEvPtr;

    /*
     * Ignore unwanted or duplicate events.
     */

    if (!(filePtr->mask & mask) || (filePtr->readyMask & mask)) {
	return;
    }

    /*
     * This is an interesting event, so put it onto the event queue.
     */

    filePtr->readyMask |= mask;
    fileEvPtr = ckalloc(sizeof(FileHandlerEvent));
    fileEvPtr->header.proc = FileHandlerEventProc;
    fileEvPtr->fd = filePtr->fd;
    Tcl_QueueEvent((Tcl_Event *) fileEvPtr, TCL_QUEUE_TAIL);

    /*
     * Process events on the Tcl event queue before returning to Xt.
     */

    Tcl_ServiceAll();
}

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
            filePtr->read = PyEvents_CreateFileHandler(fd, mask, TclFileProc, filePtr);
        }
    } else {
        if (filePtr->mask & TCL_READABLE) {
            PyEvents_DeleteFileHandler(filePtr->read);
        }
    }
    if (mask & TCL_WRITABLE) {
        if (!(filePtr->mask & TCL_WRITABLE)) {
            filePtr->write = PyEvents_CreateFileHandler(fd, mask, TclFileProc, filePtr);
        }
    } else {
        if (filePtr->mask & TCL_WRITABLE) {
            PyEvents_DeleteFileHandler(filePtr->write);
        }
    }
    if (mask & TCL_EXCEPTION) {
        if (!(filePtr->mask & TCL_EXCEPTION)) {
            filePtr->except = PyEvents_CreateFileHandler(fd, mask, TclFileProc, filePtr);
        }
    } else {
        if (filePtr->mask & TCL_EXCEPTION) {
            PyEvents_DeleteFileHandler(filePtr->except);
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
    InitNotifier();
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
