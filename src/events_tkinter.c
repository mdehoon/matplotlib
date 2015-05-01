#include <Python.h>
#include <tcl.h>
#include "events.h"

#if PY_MAJOR_VERSION >= 3
#define PY3K 1
#else
#define PY3K 0
#endif


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
    PyObject* read;             /* events.Socket object. */
    PyObject* write;            /* events.Socket object. */
    PyObject* except;           /* events.Socket object. */
    void(*proc)(void*, int);    /* Procedure to call */
    void* clientData;           /* Argument to pass to proc. */
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

static PyObject *module = NULL;

static void
NotifierExitHandler(
    ClientData clientData)      /* Not used. */
{
    PyObject* function;
    PyObject* result;
    PyObject* exception_type;
    PyObject* exception_value;
    PyObject* exception_traceback;
    PyGILState_STATE gstate;
    if (notifier.currentTimeout != NULL) {
        gstate = PyGILState_Ensure();
        PyErr_Fetch(&exception_type, &exception_value, &exception_traceback);
        function = PyObject_GetAttrString(module, "remove_timer");
        if (function) {
            result = PyObject_CallFunction(function, "O", notifier.currentTimeout);
            if(result)Py_DECREF(result);
            else PyErr_Print();
        }
        PyErr_Restore(exception_type, exception_value, exception_traceback);
        PyGILState_Release(gstate);
        Py_DECREF(notifier.currentTimeout);
        notifier.currentTimeout = NULL;
    }
    for (; notifier.firstFileHandlerPtr != NULL; ) {
        Tcl_DeleteFileHandler(notifier.firstFileHandlerPtr->fd);
    }
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
    long i = -1;
    PyObject* result;
    PyObject* function;
    PyObject* exception_type;
    PyObject* exception_value;
    PyObject* exception_traceback;
    PyGILState_STATE gstate;
    unsigned long milliseconds = ULONG_MAX;
    if (timePtr)
        milliseconds = timePtr->sec * 1000 + timePtr->usec / 1000;
    gstate = PyGILState_Ensure();
    PyErr_Fetch(&exception_type, &exception_value, &exception_traceback);
    function = PyObject_GetAttrString(module, "wait_for_event");
    if (function) {
        result = PyObject_CallFunction(function, "k", milliseconds);
        Py_DECREF(function);
        if (result) {
            if (PyInt_Check(result)) i = PyInt_AS_LONG(result);
            else PyErr_SetString(PyExc_RuntimeError,
                                 "wait_for_event failed to return an integer");
            Py_DECREF(result);
        }
    }
    if (i==-1) PyErr_Print();
    PyErr_Restore(exception_type, exception_value, exception_traceback);
    PyGILState_Release(gstate);
    return i;
}

static PyObject *TimerProc(PyObject *unused, PyObject *timer)
{
    if (timer != notifier.currentTimeout) {
        /* this is not supposed to happen */
        PyErr_SetString(PyExc_RuntimeError, "timer mismatch in callback");
        return NULL;
    }
    Py_DECREF(notifier.currentTimeout);
    notifier.currentTimeout = NULL;
    Tcl_ServiceAll();
    Py_INCREF(Py_None);
    return Py_None;
}

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

static PyObject *FileProc(PyObject *unused, PyObject *args)
{
    int fd;
    int mask;
    FileHandler* filePtr;
    FileHandlerEvent *fileEvPtr;
    if (!PyArg_ParseTuple(args, "ii", &fd, &mask)) return NULL;
    for (filePtr = notifier.firstFileHandlerPtr; filePtr != NULL;
            filePtr = filePtr->nextPtr) {
        if (filePtr->fd == fd) {
            break;
        }
    }
    if (filePtr == NULL) {
        /* This is not supposed to happen. */
        PyErr_SetString(PyExc_RuntimeError, "file descriptor mismatch in callback");
        return NULL;
    }

    /*
     * Ignore unwanted or duplicate events.
     */

    if ((filePtr->mask & mask) && !(filePtr->readyMask & mask)) {
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
    Py_INCREF(Py_None);
    return Py_None;
}

static struct PyMethodDef timer_callback_descr = {
  "timer_proc",
  (PyCFunction)TimerProc,
  METH_O,
  NULL
};

static struct PyMethodDef file_callback_descr = {
  "file_proc",
  (PyCFunction)FileProc,
  METH_VARARGS,
  NULL
};

static PyObject *timer_callback;
static PyObject *file_callback;

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
    PyObject* function;
    PyObject* result = NULL;
    PyObject* exception_type;
    PyObject* exception_value;
    PyObject* exception_traceback;
    PyGILState_STATE gstate;
    if (notifier.currentTimeout != NULL) {
        gstate = PyGILState_Ensure();
        PyErr_Fetch(&exception_type, &exception_value, &exception_traceback);
        function = PyObject_GetAttrString(module, "remove_timer");
        if (function) {
            result = PyObject_CallFunction(function, "O", notifier.currentTimeout);
            Py_DECREF(function);
        }
        if (result) Py_DECREF(result);
        else PyErr_Print();
        PyErr_Restore(exception_type, exception_value, exception_traceback);
        PyGILState_Release(gstate);
        Py_DECREF(notifier.currentTimeout);
    }
    if (timePtr) {
        timeout = timePtr->sec * 1000 + timePtr->usec / 1000;
        gstate = PyGILState_Ensure();
        PyErr_Fetch(&exception_type, &exception_value, &exception_traceback);
        function = PyObject_GetAttrString(module, "add_timer");
        if (function) {
            notifier.currentTimeout = PyEval_CallFunction(function, "kO", timeout, timer_callback);
            Py_DECREF(function);
        }
        if (notifier.currentTimeout==NULL) PyErr_Print();
        PyErr_Restore(exception_type, exception_value, exception_traceback);
        PyGILState_Release(gstate);
    } else {
        notifier.currentTimeout = NULL;
    }
}

static ClientData InitNotifier(void)
{
    Tcl_SetServiceMode(TCL_SERVICE_ALL);
    return NULL;
}

static void ServiceModeHook(int mode)
{
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
    PyGILState_STATE gstate;
    PyObject* result;
    PyObject* exception_type;
    PyObject* exception_value;
    PyObject* exception_traceback;
    PyObject* create_socket = NULL;
    PyObject* delete_socket = NULL;
    FileHandler* filePtr;
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
    filePtr->proc = proc;
    filePtr->clientData = clientData;
    gstate = PyGILState_Ensure();
    PyErr_Fetch(&exception_type, &exception_value, &exception_traceback);
    create_socket = PyObject_GetAttrString(module, "create_socket");
    if (!create_socket) goto exit;
    delete_socket = PyObject_GetAttrString(module, "delete_socket");
    if (!delete_socket) goto exit;
    if (mask & TCL_READABLE) {
        if (!(filePtr->mask & TCL_READABLE)) {
            filePtr->read = PyEval_CallFunction(create_socket, "iiO",
                                                fd,
                                                PyEvents_READABLE,
                                                file_callback);
        }
    } else {
        if (filePtr->mask & TCL_READABLE) {
            result = PyEval_CallFunction(delete_socket, "O", filePtr->read);
            Py_XDECREF(result);
            Py_DECREF(filePtr->read);
        }
    }
    if (mask & TCL_WRITABLE) {
        if (!(filePtr->mask & TCL_WRITABLE)) {
            filePtr->write = PyEval_CallFunction(create_socket, "iiO",
                                                 fd,
                                                 PyEvents_WRITABLE,
                                                 file_callback);
        }
    } else {
        if (filePtr->mask & TCL_WRITABLE) {
            result = PyEval_CallFunction(delete_socket, "O", filePtr->write);
            Py_XDECREF(result);
            Py_DECREF(filePtr->write);
        }
    }
    if (mask & TCL_EXCEPTION) {
        if (!(filePtr->mask & TCL_EXCEPTION)) {
            filePtr->except = PyEval_CallFunction(create_socket, "iiO",
                                                  fd,
                                                  PyEvents_EXCEPTION,
                                                  file_callback);
        }
    } else {
        if (filePtr->mask & TCL_EXCEPTION) {
            result = PyEval_CallFunction(delete_socket, "O", filePtr->except);
            Py_XDECREF(result);
            Py_DECREF(filePtr->except);
        }
    }
exit:
    PyErr_Restore(exception_type, exception_value, exception_traceback);
    PyGILState_Release(gstate);
    filePtr->mask = mask;
    if (!create_socket || !delete_socket) PyErr_Print();
    Py_XDECREF(create_socket);
    Py_XDECREF(delete_socket);
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
    PyGILState_STATE gstate;
    PyObject* exception_type;
    PyObject* exception_value;
    PyObject* exception_traceback;
    PyObject* delete_socket;
    PyObject* result;
    FileHandler *prevPtr;
    FileHandler* filePtr = NULL;
    gstate = PyGILState_Ensure();
    PyErr_Fetch(&exception_type, &exception_value, &exception_traceback);
    delete_socket = PyObject_GetAttrString(module, "delete_socket");
    if (!delete_socket) {
        PyErr_Print();
        goto exit;
    }

    /*
     * Find the entry for the given file (and return if there isn't one).
     */

    for (prevPtr = NULL, filePtr = notifier.firstFileHandlerPtr; ;
            prevPtr = filePtr, filePtr = filePtr->nextPtr) {
        if (filePtr == NULL) {
            goto exit;
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
        result = PyEval_CallFunction(delete_socket, "O", filePtr->read);
        Py_XDECREF(result);
        Py_DECREF(filePtr->read);
    }
    if (filePtr->mask & TCL_WRITABLE) {
        result = PyEval_CallFunction(delete_socket, "O", filePtr->write);
        Py_XDECREF(result);
        Py_DECREF(filePtr->write);
    }
    if (filePtr->mask & TCL_EXCEPTION) {
        result = PyEval_CallFunction(delete_socket, "O", filePtr->except);
        Py_XDECREF(result);
        Py_DECREF(filePtr->except);
    }

    ckfree(filePtr);
exit:
    PyErr_Restore(exception_type, exception_value, exception_traceback);
    PyGILState_Release(gstate);
}

static struct PyMethodDef methods[] = {
   {NULL,          NULL, 0, NULL} /* sentinel */
};

#if PY3K
static void freeevents_tkinter(void* module)
{
    Tcl_NotifierProcs np;
    memset(&np, 0, sizeof(np));
    Tcl_SetNotifier(&np);
}

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,      /* m_base */
    "events_tkinter",           /* m_name */
    "events_tkinter module",    /* m_doc */
    -1,                         /* m_size */
    methods,                    /* m_methods */
    NULL,                       /* m_reload */
    NULL,                       /* m_traverse */
    NULL,                       /* m_clear */
    freeevents_tkinter          /* m_free */
};

PyObject* PyInit_events_tkinter(void)

#else

void initevents_tkinter(void)
#endif
{
    Tcl_NotifierProcs np;
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
    memset(&np, 0, sizeof(np));
    np.initNotifierProc = InitNotifier;
    np.serviceModeHookProc = ServiceModeHook;
    np.createFileHandlerProc = CreateFileHandler;
    np.deleteFileHandlerProc = DeleteFileHandler;
    np.setTimerProc = SetTimer;
    np.waitForEventProc = WaitForEvent;
    Tcl_SetNotifier(&np);
    Tcl_CreateExitHandler(NotifierExitHandler, NULL);
    timer_callback = PyCFunction_New(&timer_callback_descr, NULL);
    file_callback = PyCFunction_New(&file_callback_descr, NULL);
#if PY3K
    return module;
#endif
}
