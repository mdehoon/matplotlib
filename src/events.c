#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/StringDefs.h>
#include <X11/Xaw/Command.h>
#include <Python.h>
#define EVENTS_MODULE
#include "events.h"

#if PY_MAJOR_VERSION >= 3
#define PY3K 1
#else
#if PY_MINOR_VERSION < 7
#error Python version should be 2.7 or newer
#else
#define PY3K 0
#endif
#endif

static struct NotifierState {
    XtAppContext appContext;    /* The context used by the Xt notifier. */
    Observer* observers[2];
    int nobservers[2];
} notifier;

typedef struct {
    PyObject_HEAD
    XtIntervalId timer;
    void(*callback)(PyObject*);
} TimerObject;

static PyTypeObject TimerType = {
    PyVarObject_HEAD_INIT(NULL, 0)
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

static void
PyEvents_AddObserver(int activity, Observer observer)
{
    int n;
    if (activity < 0 || activity > 1) return;
    n = notifier.nobservers[activity];
    notifier.observers[activity] = realloc(notifier.observers[activity], n+1);
    notifier.observers[activity][n] = observer;
    notifier.nobservers[activity] = n+1;
}

static void
PyEvents_RemoveObserver(int activity, Observer observer)
{
    int n;
    int i;
    if (activity < 0 || activity > 1) return;
    n = notifier.nobservers[activity];
    for (i = 0; i < n; i++)
        if (notifier.observers[activity][i] == observer)
            break;
    for ( ; i < n-1; i++)
        notifier.observers[activity][i] = notifier.observers[activity][i+1];
    notifier.nobservers[activity] = n-1;
}

typedef struct {
    PyObject_HEAD
    XtInputId input;
    void(*proc)(void* info, int mask);
    void* info;
    int mask;
} SocketObject;

static void
FileProc(XtPointer clientData, int *fd, XtInputId *id)
{
    SocketObject* object = (SocketObject*)clientData;
    void(*proc)(void* info, int mask) = object->proc;
    void* info = object->info;
    int mask = object->mask;
    return proc(info, mask);
}

static PyTypeObject SocketType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "events.Socket",           /*tp_name*/
    sizeof(SocketObject),      /*tp_basicsize*/
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
    "Socket object",           /*tp_doc */
};

static PyObject*
PyEvents_CreateSocket(
    int fd,			/* Handle of stream to watch. */
    int mask,			/* OR'ed combination of PyEvents_READABLE,
				 * PyEvents_WRITABLE, and PyEvents_EXCEPTION:
                                 * indicates conditions under which proc
                                 * should be called. */
    void(*proc)(void* info, int mask),
    void* argument)		/* Arbitrary data to pass to proc. */
{
    XtInputId input;
    XtPointer condition;
    SocketObject* object;
    switch (mask) {
        case PyEvents_READABLE:
            condition = (XtPointer)XtInputReadMask; break;
        case PyEvents_WRITABLE:
            condition = (XtPointer)XtInputWriteMask; break;
        case PyEvents_EXCEPTION:
            condition = (XtPointer)XtInputExceptMask; break;
        default: return 0;
    }
    object = (SocketObject*)PyType_GenericNew(&SocketType, NULL, NULL);
    input = XtAppAddInput(notifier.appContext, fd, condition, FileProc, (XtPointer)object);
    object->proc = proc;
    object->info = argument;
    object->mask = mask;
    object->input = input;
    return (PyObject*)object;
}

static void
PyEvents_DeleteSocket(PyObject* argument)
{
    if (argument && PyObject_TypeCheck(argument, &SocketType))
    {
        SocketObject* object = (SocketObject*)argument;
        XtInputId input = object->input;
        if (input) {
            XtRemoveInput(input);
            object->input = 0;
        }
    }
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
    int i;
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
    for (i = 0; i < notifier.nobservers[0]; i++) {
        Observer* observer = notifier.observers[i];
        (*observer)();
    }
    sig_t py_sigint_catcher = PyOS_setsig(SIGINT, sigint_catcher);
    sigint_handler_id = XtAppAddSignal(context, sigint_handler, &interrupted);
    while (!input_available && !interrupted) {
        XtAppProcessEvent(context, XtIMAll);
    }
    PyOS_setsig(SIGINT, py_sigint_catcher);
    XtRemoveSignal(sigint_handler_id);
    for (i = 0; i < notifier.nobservers[1]; i++) {
        Observer* observer = notifier.observers[i];
        (*observer)();
    }
    XtRemoveInput(input);
    if (interrupted) {
        errno = EINTR;
        raise(SIGINT);
        return -1;
    }
    return 1;
}

static void Action (Widget w, XtPointer client_data, XtPointer call_data) {
  fprintf (stderr, "You pressed me.\n");
}

static PyObject*
test(PyObject* self)
{
    Widget win, button;
    int argc = 0;
    Display* d = XtOpenDisplay(notifier.appContext,
                               NULL,
                               NULL,
                               "Python events module / Xt",
                               NULL,
                               0,
                               &argc,
                               NULL);
    win = XtAppCreateShell(NULL,
                           NULL,
                           applicationShellWidgetClass,
                           d,
                           NULL,
                           0);
    button = XtVaCreateWidget (
                "XtButton",
                commandWidgetClass,
                win,
                NULL);
    XtManageChild(button);
    XtAddCallback(button, XtNcallback, Action, 0);
    XtRealizeWidget(win);
    Py_INCREF(Py_None);
    return Py_None;
}

static struct PyMethodDef methods[] = {
   {"test",
    (PyCFunction)test,
    METH_NOARGS,
    "open a test window."
   },
   {NULL,          NULL, 0, NULL} /* sentinel */
};

#if PY3K
static void freeevents(void* module)
{
    XtDestroyApplicationContext(notifier.appContext);
    notifier.appContext = NULL;
    PyOS_InputHook = NULL;
}

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,  /* m_base */
    "events",               /* m_name */
    "events module",        /* m_doc */
    -1,                     /* m_size */
    methods,                /* m_methods */
    NULL,                   /* m_reload */
    NULL,                   /* m_traverse */
    NULL,                   /* m_clear */
    freeevents              /* m_free */
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
    if (PyType_Ready(&SocketType) < 0)
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
    PyEvents_API[PyEvents_CreateSocket_NUM] = (void *)PyEvents_CreateSocket;
    PyEvents_API[PyEvents_DeleteSocket_NUM] = (void *)PyEvents_DeleteSocket;
    PyEvents_API[PyEvents_AddObserver_NUM] = (void *)PyEvents_AddObserver;
    PyEvents_API[PyEvents_RemoveObserver_NUM] = (void *)PyEvents_RemoveObserver;
    c_api_object = PyCapsule_New((void *)PyEvents_API, "events._C_API", NULL);
    if (c_api_object != NULL)
        PyModule_AddObject(module, "_C_API", c_api_object);
    XtToolkitInitialize();
    notifier.observers[0] = NULL;
    notifier.observers[1] = NULL;
    notifier.nobservers[0] = 0;
    notifier.nobservers[1] = 0;
    notifier.appContext = XtCreateApplicationContext();
    PyOS_InputHook = wait_for_stdin;
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
