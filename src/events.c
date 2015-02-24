#include <signal.h>
#include <X11/Intrinsic.h>
#include <Python.h>
#define EVENTS_MODULE
#include "events.h"
#include <tcl.h>

#if PY_MAJOR_VERSION >= 3
#define PY3K 1
#else
#define PY3K 0
#endif

extern int TclInExit(void); /* private function? */

/*
 * This structure is used to keep track of the notifier info for a a
 * registered file.
 */

typedef struct FileHandler {
    int fd;
    int mask;			/* Mask of desired events: TCL_READABLE,
				 * etc. */
    int readyMask;		/* Events that have been seen since the last
				 * time FileHandlerEventProc was called for
				 * this file. */
    XtInputId read;		/* Xt read callback handle. */
    XtInputId write;		/* Xt write callback handle. */
    XtInputId except;		/* Xt exception callback handle. */
    void(*proc)(void*, int);	/* Procedure to call */
    void* clientData;		/* Argument to pass to proc. */
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

struct NotifierState {
    XtAppContext appContext;    /* The context used by the Xt notifier. */
    PyObject* currentTimeout;/* Handle of current timer. */
    FileHandler *firstFileHandlerPtr;
                                /* Pointer to head of file handler list. */
} notifier;

/*
 * The following static indicates whether this module has been initialized.
 */

int initialized = 0;

/*
 * Static routines defined in this file.
 */

static int              FileHandlerEventProc(Tcl_Event *evPtr, int flags);
static void             TclFileProc(XtPointer clientData, int *source, int mask);
static void             NotifierExitHandler(ClientData clientData);

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
}

typedef struct {
    PyObject_HEAD
    XtIntervalId timer;
    void(*callback)(PyObject*);
} TimerObject;

static PyTypeObject TimerType = {
    PyObject_HEAD_INIT(NULL)
    0,                         /*ob_size*/
    "events.Timer",            /*tp_name*/
    sizeof(TimerObject),       /*tp_basicsize*/
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
    "Timer object",            /*tp_doc */
};

static void
TimerProc(XtPointer clientData, XtIntervalId *id)
{
    void(*callback)(PyObject*);
    TimerObject* object = (TimerObject*)clientData;
    if (object->timer != *id) {
        /* this is not supposed to happen */
        return;
    }
    object->timer = 0;
    callback = object->callback;
    callback((PyObject*)object);
}

static PyObject*
PyEvents_AddTimer(unsigned long timeout, void(*callback)(PyObject*))
{
    TimerObject* object;
    XtIntervalId timer;
    object = (TimerObject*)PyType_GenericNew(&TimerType, NULL, NULL);
    timer = XtAppAddTimeOut(notifier.appContext, timeout, TimerProc, object);
    object->timer = timer;
    object->callback = callback;
    return (PyObject*)object;
}

static void
PyEvents_RemoveTimer(PyObject* argument)
{
    if (argument && PyObject_TypeCheck(argument, &TimerType))
    {
        TimerObject* object = (TimerObject*)argument;
        XtIntervalId timer = object->timer;
        if (timer) {
            XtRemoveTimeOut(timer);
            object->timer = 0;
        }
    }
}

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

static void
TclFileProc(
    XtPointer clientData,
    int *fd,
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

static void
ReadFileProc(
    XtPointer clientData,
    int *fd,
    XtInputId *id)
{
    int mask = TCL_READABLE;
    return TclFileProc(clientData, fd, mask);
}

static void
WriteFileProc(
    XtPointer clientData,
    int *fd,
    XtInputId *id)
{
    int mask = TCL_WRITABLE;
    return TclFileProc(clientData, fd, mask);
}

static void
ExceptionFileProc(
    XtPointer clientData,
    int *fd,
    XtInputId *id)
{
    int mask = TCL_EXCEPTION;
    return TclFileProc(clientData, fd, mask);
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

typedef struct {
    PyObject_HEAD
    ClientData clientData;
} FileHandlerDataObject;

static void
PyEvents_CreateFileHandler(
    int fd,			/* Handle of stream to watch. */
    int mask,			/* OR'ed combination of TCL_READABLE,
				 * TCL_WRITABLE, and TCL_EXCEPTION: indicates
				 * conditions under which proc should be
				 * called. */
    void(*proc)(void*, int),	/* Procedure to call for each selected
				 * event. */
    PyObject* argument)		/* Arbitrary data to pass to proc. */
{
    FileHandler *filePtr;
    FileHandlerDataObject* object = (FileHandlerDataObject*)argument;

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

    /*
     * Register the file with the Xt notifier, if it hasn't been done yet.
     */

    if (mask & TCL_READABLE) {
	if (!(filePtr->mask & TCL_READABLE)) {
	    filePtr->read = XtAppAddInput(notifier.appContext, fd,
		    (XtPointer) (XtInputReadMask), ReadFileProc, filePtr);
	}
    } else {
	if (filePtr->mask & TCL_READABLE) {
	    XtRemoveInput(filePtr->read);
	}
    }
    if (mask & TCL_WRITABLE) {
	if (!(filePtr->mask & TCL_WRITABLE)) {
	    filePtr->write = XtAppAddInput(notifier.appContext, fd,
		    (XtPointer) (XtInputWriteMask), WriteFileProc, filePtr);
	}
    } else {
	if (filePtr->mask & TCL_WRITABLE) {
	    XtRemoveInput(filePtr->write);
	}
    }
    if (mask & TCL_EXCEPTION) {
	if (!(filePtr->mask & TCL_EXCEPTION)) {
	    filePtr->except = XtAppAddInput(notifier.appContext, fd,
		    (XtPointer) (XtInputExceptMask), ExceptionFileProc, filePtr);
	}
    } else {
	if (filePtr->mask & TCL_EXCEPTION) {
	    XtRemoveInput(filePtr->except);
	}
    }
    filePtr->mask = mask;
}

static void
PyEvents_DeleteFileHandler(
    int fd)			/* Stream id for which to remove callback
				 * procedure. */
{
    FileHandler *filePtr, *prevPtr;

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

    /*
     * Clean up information in the callback record.
     */

    if (prevPtr == NULL) {
	notifier.firstFileHandlerPtr = filePtr->nextPtr;
    } else {
	prevPtr->nextPtr = filePtr->nextPtr;
    }
    if (filePtr->mask & TCL_READABLE) {
	XtRemoveInput(filePtr->read);
    }
    if (filePtr->mask & TCL_WRITABLE) {
	XtRemoveInput(filePtr->write);
    }
    if (filePtr->mask & TCL_EXCEPTION) {
	XtRemoveInput(filePtr->except);
    }
    Py_XDECREF(filePtr->argument);
    ckfree(filePtr);
}

static int
PyEvents_HavePendingEvents(void)
{
    if (XtAppPending(notifier.appContext)==0) return 0;
    return 1;
}

static void
PyEvents_ProcessEvent(void)
{
    XtAppProcessEvent(notifier.appContext, XtIMAll);
}

static void stdin_callback(XtPointer client_data, int* source, XtInputId* id)
{
    int* input_available = client_data;
    *input_available = 1;
}

static XtSignalId sigint_handler_id;

static void sigint_handler(XtPointer client_data, XtSignalId* id)
{
    int* interrupted = client_data;
    *interrupted = 1;
}

static void sigint_catcher(int signo)
{
    XtNoticeSignal(sigint_handler_id);
}

static int wait_for_stdin(void)
{
    int interrupted = 0;
    int input_available = 0;
    int fd = fileno(stdin);
    XtAppContext context;
    if (!initialized) {
	InitNotifier();
    }
    context =  notifier.appContext;
    XtInputId input = XtAppAddInput(context,
                                    fd,
                                    (XtPointer)XtInputReadMask,
                                    stdin_callback,
                                    &input_available);
    int old_mode = Tcl_SetServiceMode(TCL_SERVICE_ALL);
    sig_t py_sigint_catcher = PyOS_setsig(SIGINT, sigint_catcher);
    sigint_handler_id = XtAppAddSignal(context, sigint_handler, &interrupted);
    while (!input_available && !interrupted) {
        XtAppProcessEvent(context, XtIMAll);
    }
    PyOS_setsig(SIGINT, py_sigint_catcher);
    XtRemoveSignal(sigint_handler_id);
    Tcl_SetServiceMode(old_mode);
    XtRemoveInput(input);
    if (interrupted) {
        errno = EINTR;
        raise(SIGINT);
        return -1;
    }
    return 1;
}

static unsigned int started = 0;

static PyObject*
start(PyObject* unused)
{
    if (started==0) {
        notifier.appContext = XtCreateApplicationContext();
        PyOS_InputHook = wait_for_stdin;
    }
    started++;
    return PyLong_FromLong(started);
}

static PyObject*
stop(PyObject* unused)
{
    started--;
    if (started==0) {
        XtDestroyApplicationContext(notifier.appContext);
        notifier.appContext = NULL;
        PyOS_InputHook = NULL;
    }
    return PyLong_FromLong(started);
}

static struct PyMethodDef methods[] = {
   {"start",
    (PyCFunction)start,
    METH_NOARGS,
    "starts the Xt event loop"
   },
   {"stop",
    (PyCFunction)stop,
    METH_NOARGS,
    "stops the Xt event loop"
   },
   {NULL,          NULL, 0, NULL} /* sentinel */
};

#if PY3K
static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "events",
    "events module",
    -1,
    methods,
    NULL,
    NULL,
    NULL,
    NULL
};

PyObject* PyInit_events(void)

#else

void initevents(void)
#endif
{
    PyObject *module;
    static void *PyEvents_API[PyEvents_API_pointers];
    PyObject* c_api_object;

    if (PyType_Ready(&TimerType) < 0)
        goto error;
#if PY3K
    module = PyModule_Create(&moduledef);
#else
    module = Py_InitModule4("events",
                            methods,
                            "events module",
                            NULL,
                            PYTHON_API_VERSION);
#endif
    if (module==NULL) goto error;
    PyEvents_API[PyEvents_AddTimer_NUM] = (void *)PyEvents_AddTimer;
    PyEvents_API[PyEvents_RemoveTimer_NUM] = (void *)PyEvents_RemoveTimer;
    PyEvents_API[PyEvents_ProcessEvent_NUM] = (void *)PyEvents_ProcessEvent;
    PyEvents_API[PyEvents_HavePendingEvents_NUM] = (void *)PyEvents_HavePendingEvents;
    PyEvents_API[PyEvents_CreateFileHandler_NUM] = (void *)PyEvents_CreateFileHandler;
    PyEvents_API[PyEvents_DeleteFileHandler_NUM] = (void *)PyEvents_DeleteFileHandler;
    c_api_object = PyCapsule_New((void *)PyEvents_API, "events._C_API", NULL);
    if (c_api_object != NULL)
        PyModule_AddObject(module, "_C_API", c_api_object);
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
