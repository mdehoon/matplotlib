#include <signal.h>
#include <X11/Intrinsic.h>
#include <Python.h>
#include <tcl.h>
#include <tk.h>

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
    Tcl_FileProc *proc;		/* Procedure to call, in the style of
				 * Tcl_CreateFileHandler. */
    ClientData clientData;	/* Argument to pass to proc. */
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
    XtAppContext appContext;    /* The context used by the Xt notifier. Can be
                                 * set with TclSetAppContext. */
    XtIntervalId currentTimeout;/* Handle of current timer. */
    FileHandler *firstFileHandlerPtr;
                                /* Pointer to head of file handler list. */
} notifier;

/*
 * The following static indicates whether this module has been initialized.
 */

static int initialized = 0;

/*
 * Static routines defined in this file.
 */

static int              FileHandlerEventProc(Tcl_Event *evPtr, int flags);
static void             FileProc(XtPointer clientData, int *source,
                            XtInputId *id);
static void             NotifierExitHandler(ClientData clientData);
static void             TimerProc(XtPointer clientData, XtIntervalId *id);
static void             CreateFileHandler(int fd, int mask,
                            Tcl_FileProc *proc, ClientData clientData);
static void             DeleteFileHandler(int fd);
static void             SetTimer(const Tcl_Time * timePtr);
static int              WaitForEvent(const Tcl_Time * timePtr);



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

    /*
     * DO NOT create the application context yet; doing so would prevent
     * external applications from setting it for us to their own ones.
     */

    initialized = 1;
    Tcl_CreateExitHandler(NotifierExitHandler, NULL);
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
        XtRemoveTimeOut(notifier.currentTimeout);
    }
    for (; notifier.firstFileHandlerPtr != NULL; ) {
        Tcl_DeleteFileHandler(notifier.firstFileHandlerPtr->fd);
    }
    initialized = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * TclSetAppContext --
 *
 *	Set the notifier application context.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Sets the application context used by the notifier. Panics if the
 *	context is already set when called.
 *
 *----------------------------------------------------------------------
 */

XtAppContext
TclSetAppContext(void)
{
    if (!initialized) {
	InitNotifier();
    }

    return notifier.appContext;
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
FileProc(
    XtPointer clientData,
    int *fd,
    XtInputId *id)
{
    FileHandler *filePtr = (FileHandler *)clientData;
    FileHandlerEvent *fileEvPtr;
    int mask = 0;

    /*
     * Determine which event happened.
     */

    if (*id == filePtr->read) {
	mask = TCL_READABLE;
    } else if (*id == filePtr->write) {
	mask = TCL_WRITABLE;
    } else if (*id == filePtr->except) {
	mask = TCL_EXCEPTION;
    }

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
 * SetTimer --
 *
 *	This procedure sets the current notifier timeout value.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Replaces any previous timer.
 *
 *----------------------------------------------------------------------
 */

static void
SetTimer(
    const Tcl_Time *timePtr)		/* Timeout value, may be NULL. */
{
    long timeout;

    if (!initialized) {
	InitNotifier();
    }

    TclSetAppContext();
    if (notifier.currentTimeout != 0) {
	XtRemoveTimeOut(notifier.currentTimeout);
    }
    if (timePtr) {
	timeout = timePtr->sec * 1000 + timePtr->usec / 1000;
	notifier.currentTimeout = XtAppAddTimeOut(notifier.appContext,
		(unsigned long) timeout, TimerProc, NULL);
    } else {
	notifier.currentTimeout = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TimerProc --
 *
 *	This procedure is the XtTimerCallbackProc used to handle timeouts.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Processes all queued events.
 *
 *----------------------------------------------------------------------
 */

static void
TimerProc(
    XtPointer clientData, /* Not used. */
    XtIntervalId *id)
{
    if (*id != notifier.currentTimeout) {
	return;
    }
    notifier.currentTimeout = 0;

    Tcl_ServiceAll();
}

/*
 *----------------------------------------------------------------------
 *
 * CreateFileHandler --
 *
 *	This procedure registers a file handler with the Xt notifier.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Creates a new file handler structure and registers one or more input
 *	procedures with Xt.
 *
 *----------------------------------------------------------------------
 */

static void
CreateFileHandler(
    int fd,			/* Handle of stream to watch. */
    int mask,			/* OR'ed combination of TCL_READABLE,
				 * TCL_WRITABLE, and TCL_EXCEPTION: indicates
				 * conditions under which proc should be
				 * called. */
    Tcl_FileProc *proc,		/* Procedure to call for each selected
				 * event. */
    ClientData clientData)	/* Arbitrary data to pass to proc. */
{
    FileHandler *filePtr;

    if (!initialized) {
	InitNotifier();
    }

    TclSetAppContext();

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

    /*
     * Register the file with the Xt notifier, if it hasn't been done yet.
     */

    if (mask & TCL_READABLE) {
	if (!(filePtr->mask & TCL_READABLE)) {
	    filePtr->read = XtAppAddInput(notifier.appContext, fd,
		    (XtPointer) (XtInputReadMask), FileProc, filePtr);
	}
    } else {
	if (filePtr->mask & TCL_READABLE) {
	    XtRemoveInput(filePtr->read);
	}
    }
    if (mask & TCL_WRITABLE) {
	if (!(filePtr->mask & TCL_WRITABLE)) {
	    filePtr->write = XtAppAddInput(notifier.appContext, fd,
		    (XtPointer) (XtInputWriteMask), FileProc, filePtr);
	}
    } else {
	if (filePtr->mask & TCL_WRITABLE) {
	    XtRemoveInput(filePtr->write);
	}
    }
    if (mask & TCL_EXCEPTION) {
	if (!(filePtr->mask & TCL_EXCEPTION)) {
	    filePtr->except = XtAppAddInput(notifier.appContext, fd,
		    (XtPointer) (XtInputExceptMask), FileProc, filePtr);
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
 *	Cancel a previously-arranged callback arrangement for a file.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	If a callback was previously registered on file, remove it.
 *
 *----------------------------------------------------------------------
 */

static void
DeleteFileHandler(
    int fd)			/* Stream id for which to remove callback
				 * procedure. */
{
    FileHandler *filePtr, *prevPtr;

    if (!initialized) {
	InitNotifier();
    }

    TclSetAppContext();

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
    ckfree(filePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * WaitForEvent --
 *
 *	This function is called by Tcl_DoOneEvent to wait for new events on
 *	the message queue. If the block time is 0, then Tcl_WaitForEvent just
 *	polls without blocking.
 *
 * Results:
 *	Returns 1 if an event was found, else 0. This ensures that
 *	Tcl_DoOneEvent will return 1, even if the event is handled by non-Tcl
 *	code.
 *
 * Side effects:
 *	Queues file events that are detected by the select.
 *
 *----------------------------------------------------------------------
 */

static int
WaitForEvent(
    const Tcl_Time *timePtr)		/* Maximum block time, or NULL. */
{
    int timeout;

    if (!initialized) {
	InitNotifier();
    }

    TclSetAppContext();

    if (timePtr) {
	timeout = timePtr->sec * 1000 + timePtr->usec / 1000;
	if (timeout == 0) {
	    if (XtAppPending(notifier.appContext)==0) {
		return 0;
	    }
	} else {
	    Tcl_SetTimer(timePtr);
	}
    }

    XtAppProcessEvent(notifier.appContext, XtIMAll);
    return 1;
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
    XtAppContext context = TclSetAppContext();
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

static unsigned int started = 0;

static PyObject*
start(PyObject* unused)
{
    if (started==0) notifier.appContext = XtCreateApplicationContext();
    started++;
    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject*
stop(PyObject* unused)
{
    started--;
    if (started==0) {
        XtDestroyApplicationContext(notifier.appContext);
        notifier.appContext = NULL;
    }
    Py_INCREF(Py_None);
    return Py_None;
}

static struct PyMethodDef methods[] = {
   {"load",
    (PyCFunction)load,
    METH_NOARGS,
    "loads the Xt notifier"
   },
   {"unload",
    (PyCFunction)unload,
    METH_NOARGS,
    "unloads the Xt notifier"
   },
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
#if PY3K
    module = PyModule_Create(&moduledef);
    if (module==NULL) return NULL;
#else
    module = Py_InitModule4("events",
                            methods,
                            "events module",
                            NULL,
                            PYTHON_API_VERSION);
#endif
    InitNotifier();
    PyOS_InputHook = wait_for_stdin;
#if PY3K
    return module;
#endif
}
