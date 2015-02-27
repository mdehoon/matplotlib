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

typedef struct {
    void(*proc)(void* info, int mask);
    void* info;
    int mask;
} FileContext;

static void
FileProc(XtPointer clientData,int *fd, XtInputId *id)
{
    FileContext* context = (FileContext*)clientData;
    void(*proc)(void* info, int mask) = context->proc;
    void* info = context->info;
    int mask = context->mask;
    return proc(info, mask);
}

typedef struct {
    PyObject_HEAD
    ClientData clientData;
} FileHandlerDataObject;

static XtInputId
PyEvents_CreateFileHandler(
    int fd,			/* Handle of stream to watch. */
    int mask,			/* OR'ed combination of TCL_READABLE,
				 * TCL_WRITABLE, and TCL_EXCEPTION: indicates
				 * conditions under which proc should be
				 * called. */
    void(*proc)(void* info, int mask),
    void* argument)		/* Arbitrary data to pass to proc. */
{
    XtPointer condition;
    FileContext* context = malloc(sizeof(FileContext));
    context->proc = proc;
    context->info = argument;
    context->mask = mask;
    switch (mask) {
        case TCL_READABLE: condition = (XtPointer)XtInputReadMask; break;
        case TCL_WRITABLE: condition = (XtPointer)XtInputWriteMask; break;
        case TCL_EXCEPTION: condition = (XtPointer)XtInputExceptMask; break;
        default: return 0;
    }
    return XtAppAddInput(notifier.appContext, fd, condition, FileProc, context);
}

static void
PyEvents_DeleteFileHandler(
    XtInputId id)		/* Stream id for which to remove callback
				 * procedure. */
{
    XtRemoveInput(id);
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
