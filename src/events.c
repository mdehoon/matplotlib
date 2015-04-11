#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
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

typedef struct TimerObject TimerObject;

typedef struct SocketObject SocketObject;

struct TimerObject {
    PyObject_HEAD
    unsigned long time;
    PyObject* callback;
    TimerObject* next;
};

static struct NotifierState {
    TimerObject* firstTimer;
    SocketObject* firstSocket;
} notifier;

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

static void add_timer(TimerObject* timer)
{
    TimerObject* current;
    TimerObject* next;
    if (notifier.firstTimer==NULL) notifier.firstTimer = timer;
    else {
        current = notifier.firstTimer;
        while (1) {
            next = current->next;
            if (!next) break;
            current = next;
        }
        current->next = timer;
    }
    Py_INCREF(timer);
}

static void
remove_timer(TimerObject* timer)
{
    TimerObject* previous;
    TimerObject* current;
    previous = NULL;
    current = notifier.firstTimer;
    while (1) {
        if (current==NULL) break;
        if (current==timer) {
            current = current->next;
            if (previous) previous->next = current;
            else notifier.firstTimer = current;
            Py_DECREF(timer->callback);
            Py_DECREF(timer);
            break;
        }
        previous = current;
        current = current->next;
    }
}

static unsigned long
check_timers(void)
{
    struct timeval tp;
    unsigned long now;
    unsigned long timeout = ULONG_MAX;
    long difference;
    TimerObject* timer = notifier.firstTimer;
    if (timer) {
        if (gettimeofday(&tp, NULL)==-1) {
            PyErr_Format(PyExc_RuntimeError, "gettimeofday failed unexpectedly");
            return 0;
        }
        now = 1000 * tp.tv_sec + tp.tv_usec / 1000 + timeout;
        do {
            difference = timer->time - now;
            if (difference <= 0) return 0;
            if (difference < timeout) timeout = difference;
            timer = timer->next;
        } while (timer);
    }
    return timeout;
}

static unsigned long
process_timers(void)
{
    struct timeval tp;
    unsigned long now;
    unsigned long timeout = ULONG_MAX;
    long difference;
    PyObject* arguments;
    PyObject* result;
    TimerObject* next;
    TimerObject* timer = notifier.firstTimer;
    PyGILState_STATE gstate;
    PyObject* exception_type;
    PyObject* exception_value;
    PyObject* exception_traceback;
    if (timer) {
        if (gettimeofday(&tp, NULL)==-1) {
            PyErr_Format(PyExc_RuntimeError, "gettimeofday failed unexpectedly");
            return 0;
        }
        now = 1000 * tp.tv_sec + tp.tv_usec / 1000;
        gstate = PyGILState_Ensure();
        PyErr_Fetch(&exception_type, &exception_value, &exception_traceback);
        do {
            difference = timer->time - now;
            if (difference <= 0) {
                result = NULL;
                arguments = Py_BuildValue("(O)", timer);
                if (arguments) {
                    result = PyEval_CallObject(timer->callback, arguments);
                    Py_DECREF(arguments);
                }
                if (result) Py_DECREF(result);
                else PyErr_Print();
                next = timer->next;
                remove_timer(timer);
                timer = next;
            }
            else {
                if (difference < timeout) timeout = difference;
                timer = timer->next;
            }
        } while (timer);
        PyErr_Restore(exception_type, exception_value, exception_traceback);
        PyGILState_Release(gstate);
    }
    return timeout;
}

static PyObject*
PyEvents_AddTimer(PyObject* unused, PyObject* args)
{
    TimerObject* timer;
    struct timeval tp;
    unsigned long timeout;
    PyObject* callback;
    if (!PyArg_ParseTuple(args, "kO", &timeout, &callback)) return NULL;
    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "Callback should be callable");
        return NULL;
    }
    if (gettimeofday(&tp, NULL)==-1) {
        PyErr_SetString(PyExc_RuntimeError, "gettimeofday failed unexpectedly");
        return NULL;
    }
    Py_INCREF(callback);
    timer = (TimerObject*)PyType_GenericNew(&TimerType, NULL, NULL);
    timer->time = 1000 * tp.tv_sec + tp.tv_usec / 1000 + timeout;
    timer->callback = callback;
    add_timer(timer);
    return (PyObject*)timer;
}

static PyObject*
PyEvents_RemoveTimer(PyObject* unused, PyObject* argument)
{
    TimerObject* timer;
    if (!PyObject_TypeCheck(argument, &TimerType)) {
        PyErr_SetString(PyExc_TypeError, "argument is not a timer");
        return NULL;
    }
    timer = (TimerObject*)argument;
    remove_timer(timer);
    Py_INCREF(Py_None);
    return Py_None;
}

struct SocketObject {
    PyObject_HEAD
    int fd;
    int mask;
    PyObject* callback;
    SocketObject* next;
};

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

static void add_socket(SocketObject* socket)
{
    SocketObject* current;
    SocketObject* next;
    if (notifier.firstSocket==NULL) notifier.firstSocket = socket;
    else {
        current = notifier.firstSocket;
        while (1) {
            next = current->next;
            if (!next) break;
            current = next;
        }
        current->next = socket;
    }
    Py_INCREF(socket);
}

static void remove_socket(SocketObject* socket)
{
    SocketObject* previous;
    SocketObject* current;
    previous = NULL;
    current = notifier.firstSocket;
    while (1) {
        if (current==NULL) break;
        if (current==socket) {
            current = current->next;
            if (previous) previous->next = current;
            else notifier.firstSocket = current;
            Py_DECREF(socket->callback);
            Py_DECREF(socket);
            break;
        }
        previous = current;
        current = current->next;
    }
}

static PyObject*
PyEvents_CreateSocket(PyObject* unused, PyObject* args)
{
    SocketObject* socket;
    int fd;			/* Handle of stream to watch. */
    int mask;			/* OR'ed combination of PyEvents_READABLE,
				 * PyEvents_WRITABLE, and PyEvents_EXCEPTION:
                                 * indicates conditions under which proc
                                 * should be called. */
    PyObject* callback;         /* Callback function */
    if (!PyArg_ParseTuple(args, "iiO", &fd, &mask, &callback)) return NULL;
    if (!PyCallable_Check(callback)) {
        PyErr_SetString(PyExc_TypeError, "Callback should be callable");
        return NULL;
    }
    Py_INCREF(callback);
    socket = (SocketObject*)PyType_GenericNew(&SocketType, NULL, NULL);
    socket->callback = callback;
    socket->fd = fd;
    socket->mask = mask;
    add_socket(socket);
    return (PyObject*)socket;
}

static PyObject*
PyEvents_DeleteSocket(PyObject* unused, PyObject* argument)
{
    SocketObject* socket;
    if (!PyObject_TypeCheck(argument, &SocketType)) {
        PyErr_SetString(PyExc_TypeError, "argument is not a socket");
        return NULL;
    }
    socket = (SocketObject*)argument;
    remove_socket(socket);
    Py_INCREF(Py_None);
    return Py_None;
}

static int
set_fds(fd_set* readfds, fd_set* writefds, fd_set* errorfds)
{
    int mask;
    int fd;
    int nfds = 0;
    SocketObject* socket;
    FD_ZERO(readfds);
    FD_ZERO(writefds);
    FD_ZERO(errorfds);
    socket = notifier.firstSocket;
    while (socket)
    {
        fd = socket->fd;
        mask = socket->mask;
        switch (mask) {
            case PyEvents_READABLE: FD_SET(fd, readfds); break;
            case PyEvents_WRITABLE: FD_SET(fd, writefds); break;
            case PyEvents_EXCEPTION: FD_SET(fd, errorfds); break;
        }
        if (fd > nfds) nfds = fd;
        socket = socket->next;
    }
    nfds++;
    return nfds;
}

static PyObject*
PyEvents_WaitForEvent(PyObject* unused, PyObject* args)
{
    int nfds;
    fd_set readfds;
    fd_set writefds;
    fd_set errorfds;
    struct timeval timeout;
    unsigned long waittime;
    long milliseconds;
    long result = 0;
    if (!PyArg_ParseTuple(args, "k", &milliseconds)) return NULL;
    waittime = check_timers();
    if (waittime > 0) {
        if (waittime < milliseconds) milliseconds = waittime;
        timeout.tv_sec = milliseconds / 1000;
        timeout.tv_usec = 1000 * (milliseconds % 1000);
        nfds = set_fds(&readfds, &writefds, &errorfds);
        if (select(nfds, &readfds, &writefds, &errorfds, &timeout)==-1)
            return PyErr_SetFromErrno(PyExc_RuntimeError);
    }
    return PyInt_FromLong(result);
}

static int wait_for_stdin(void)
{
    int fd;
    int mask;
    int fd_stdin = fileno(stdin);
    int ready;
    int nfds;
    long int waittime;
    fd_set readfds;
    fd_set writefds;
    fd_set errorfds;
    struct timeval timeout;
    struct timeval* ptimeout;
    SocketObject* socket;
    PyGILState_STATE gstate;
    PyObject* exception_type;
    PyObject* exception_value;
    PyObject* exception_traceback;
    PyObject* result;
    PyObject* arguments;
    while (1) {
        nfds = set_fds(&readfds, &writefds, &errorfds);
        FD_SET(fd_stdin, &readfds);
        if (fd_stdin >= nfds) nfds = fd_stdin + 1;
        waittime = process_timers();
        if (waittime == ULONG_MAX) ptimeout = NULL;
        else {
            timeout.tv_sec = waittime / 1000;
            timeout.tv_usec = 1000 * (waittime % 1000);
            ptimeout = &timeout;
        }
        if (select(nfds, &readfds, &writefds, &errorfds, ptimeout)==-1)
        {
            if (errno==EINTR) raise(SIGINT);
            return -1;
        }
        if (FD_ISSET(fd_stdin, &readfds)) break;
        socket = notifier.firstSocket;
        while (socket)
        {
            ready = 0;
            fd = socket->fd;
            mask = socket->mask;
            switch (mask) {
                case PyEvents_READABLE: ready = FD_ISSET(fd, &readfds); break;
                case PyEvents_WRITABLE: ready = FD_ISSET(fd, &writefds); break;
                case PyEvents_EXCEPTION: ready = FD_ISSET(fd, &errorfds); break;
            }
            if (ready) {
                gstate = PyGILState_Ensure();
                PyErr_Fetch(&exception_type, &exception_value, &exception_traceback);
                result = NULL;
                arguments = Py_BuildValue("(ii)", fd, mask);

                if (arguments) {
                    PyObject* callback = socket->callback;
                    result = PyEval_CallObject(callback, arguments);
                    Py_DECREF(arguments);
                }
                if (result) Py_DECREF(result);
                else PyErr_Print();
                PyErr_Restore(exception_type, exception_value, exception_traceback);
                PyGILState_Release(gstate);
            }
            socket = socket->next;
        }
    }
    return 1;
}

static struct PyMethodDef methods[] = {
    {"add_timer",
     (PyCFunction)PyEvents_AddTimer,
     METH_VARARGS,
     "add a timer."
    },
    {"remove_timer",
     (PyCFunction)PyEvents_RemoveTimer,
     METH_O,
     "remove the timer."
    },
    {"create_socket",
     (PyCFunction)PyEvents_CreateSocket,
     METH_VARARGS,
     "create a socket."
    },
    {"delete_socket",
     (PyCFunction)PyEvents_DeleteSocket,
     METH_O,
     "delete a socket."
    },
    {"wait_for_event",
     (PyCFunction)PyEvents_WaitForEvent,
     METH_VARARGS,
     "wait for an event."
    },
   {NULL,          NULL, 0, NULL} /* sentinel */
};

#if PY3K
static void freeevents(void* module)
{
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
    notifier.firstTimer = NULL;
    notifier.firstSocket = NULL;
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
