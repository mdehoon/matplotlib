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
    void(*callback)(PyObject*);
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
    void(*callback)(PyObject*);
    TimerObject* next;
    TimerObject* timer = notifier.firstTimer;
    if (timer) {
        if (gettimeofday(&tp, NULL)==-1) {
            PyErr_Format(PyExc_RuntimeError, "gettimeofday failed unexpectedly");
            return 0;
        }
        now = 1000 * tp.tv_sec + tp.tv_usec / 1000;
        do {
            difference = timer->time - now;
            if (difference <= 0) {
                callback = timer->callback;
                callback((PyObject*)timer);
                next = timer->next;
                remove_timer(timer);
                timer = next;
            }
            else {
                if (difference < timeout) timeout = difference;
                timer = timer->next;
            }
        } while (timer);
    }
    return timeout;
}

static PyObject*
PyEvents_AddTimer(unsigned long timeout, void(*callback)(PyObject*))
{
    TimerObject* timer;
    struct timeval tp;
    if (gettimeofday(&tp, NULL)==-1) {
        PyErr_Format(PyExc_RuntimeError, "gettimeofday failed unexpectedly");
        return NULL;
    }
    timer = (TimerObject*)PyType_GenericNew(&TimerType, NULL, NULL);
    timer->time = 1000 * tp.tv_sec + tp.tv_usec / 1000 + timeout;
    timer->callback = callback;
    add_timer(timer);
    return (PyObject*)timer;
}

static void
PyEvents_RemoveTimer(PyObject* argument)
{
    TimerObject* timer;
    if (!argument) return;
    if (!PyObject_TypeCheck(argument, &TimerType)) return;
    timer = (TimerObject*)argument;
    remove_timer(timer);
}

struct SocketObject {
    PyObject_HEAD
    void(*proc)(void* info, int mask);
    int fd;
    void* info;
    int mask;
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
            Py_DECREF(socket);
            break;
        }
        previous = current;
        current = current->next;
    }
}

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
    SocketObject* socket;
    socket = (SocketObject*)PyType_GenericNew(&SocketType, NULL, NULL);
    socket->proc = proc;
    socket->fd = fd;
    socket->info = argument;
    socket->mask = mask;
    add_socket(socket);
    return (PyObject*)socket;
}

static void
PyEvents_DeleteSocket(PyObject* argument)
{
    SocketObject* socket;
    if (!argument) return;
    if (!PyObject_TypeCheck(argument, &SocketType)) return;
    socket = (SocketObject*)argument;
    remove_socket(socket);
}

static int
PyEvents_HavePendingEvents(void)
{
    return 1;
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

static int
PyEvents_WaitForEvent(int milliseconds)
{
    int nfds;
    fd_set readfds;
    fd_set writefds;
    fd_set errorfds;
    struct timeval timeout;
    unsigned long waittime;
    waittime = check_timers();
    if (waittime == 0) return 0;
    if (waittime < milliseconds) milliseconds = waittime;
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_usec = 1000 * (milliseconds % 1000);
    nfds = set_fds(&readfds, &writefds, &errorfds);
    if (select(nfds, &readfds, &writefds, &errorfds, &timeout)==-1)
        /* handle error */ ;
    return 0;
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
                void(*proc)(void* info, int mask) = socket->proc;
                void* info = socket->info;
                proc(info, mask);
            }
            socket = socket->next;
        }
    }
    return 1;
}

static struct PyMethodDef methods[] = {
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
    PyEvents_API[PyEvents_HavePendingEvents_NUM] = (void *)PyEvents_HavePendingEvents;
    PyEvents_API[PyEvents_WaitForEvent_NUM] = (void *)PyEvents_WaitForEvent;
    PyEvents_API[PyEvents_CreateSocket_NUM] = (void *)PyEvents_CreateSocket;
    PyEvents_API[PyEvents_DeleteSocket_NUM] = (void *)PyEvents_DeleteSocket;
    c_api_object = PyCapsule_New((void *)PyEvents_API, "events._C_API", NULL);
    if (c_api_object != NULL)
        PyModule_AddObject(module, "_C_API", c_api_object);
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
