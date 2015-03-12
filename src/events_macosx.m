#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <Python.h>
#include <Cocoa/Cocoa.h>
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

@interface WindowServerConnectionManager : NSObject
{
}
+ (WindowServerConnectionManager*)sharedManager;
- (void)launch:(NSNotification*)notification;
@end

static struct NotifierState {
//    XtAppContext appContext;    /* The context used by the Xt notifier. */
    Observer* observers[2];
    int nobservers[2];
} notifier;

typedef struct {
    PyObject_HEAD
    CFRunLoopTimerRef timer;
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

static void timer_callback(CFRunLoopTimerRef timer, void* info)
{
    void(*callback)(PyObject*);
    TimerObject* object = (TimerObject*)info;
    if (object->timer != timer) {
        /* this is not supposed to happen */
        return;
    }
    object->timer = NULL;
    callback = object->callback;
    callback((PyObject*)object);
}

static PyObject*
PyEvents_AddTimer(unsigned long timeout, void(*callback)(PyObject*))
{
    TimerObject* object;
    CFRunLoopTimerRef timer;
    CFTimeInterval interval;
    CFAbsoluteTime fireDate;
    CFRunLoopTimerContext context;
    CFRunLoopRef runloop;
    object = (TimerObject*)PyType_GenericNew(&TimerType, NULL, NULL);
    runloop = CFRunLoopGetCurrent();
    if (!runloop) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to obtain run loop");
        return NULL;
    }
    interval = timeout / 1000.0;
    fireDate = CFAbsoluteTimeGetCurrent() + interval;
    context.version = 0;
    context.retain = 0;
    context.release = 0;
    context.copyDescription = 0;
    context.info = object;
    timer = CFRunLoopTimerCreate(kCFAllocatorDefault,
                                 fireDate,
                                 0,
                                 0,
                                 0,
                                 timer_callback,
                                 &context);
    CFRunLoopAddTimer(runloop, timer, kCFRunLoopDefaultMode);
    object->timer = timer;
    object->callback = callback;
    return (PyObject*)object;
}

static void
PyEvents_RemoveTimer(PyObject* argument)
{
    if (argument && PyObject_TypeCheck(argument, &TimerType))
    {
        CFRunLoopRef runloop;
        TimerObject* object = (TimerObject*)argument;
        CFRunLoopTimerRef timer = object->timer;
        runloop = CFRunLoopGetCurrent();
        if (timer) {
            CFRunLoopRemoveTimer(runloop, timer, kCFRunLoopDefaultMode);
            object->timer = NULL;
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
    CFRunLoopSourceRef source;
    void(*proc)(void* info, int mask);
    void* info;
    int mask;
} SocketObject;

static void
socket_callback(CFSocketRef s,
                CFSocketCallBackType callbackType,
                CFDataRef address,
                const void* data,
                void* info)
{
    SocketObject* object = info;
    void(*proc)(void* info, int mask) = object->proc;
    void* argument = object->info;
    int mask = object->mask;
    return proc(argument, mask);
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
    CFRunLoopRef runloop;
    CFRunLoopSourceRef source;
    CFSocketRef socket;
    CFSocketCallBackType condition;
    SocketObject* object;
    CFSocketContext context;
    switch (mask) {
        case PyEvents_READABLE:
            condition = kCFSocketReadCallBack; break;
        case PyEvents_WRITABLE:
            condition = kCFSocketWriteCallBack; break;
        case PyEvents_EXCEPTION:
            condition = kCFSocketNoCallBack; break;
        default: return 0;
    }
    object = (SocketObject*)PyType_GenericNew(&SocketType, NULL, NULL);
    context.version = 0;
    context.info = object;
    context.retain = 0;
    context.release = 0;
    context.copyDescription = 0;
    socket = CFSocketCreateWithNative(kCFAllocatorDefault,
                                      fd,
                                      condition,
                                      socket_callback,
                                      &context);
    source = CFSocketCreateRunLoopSource(kCFAllocatorDefault,
                                         socket,
                                         0);
    CFRelease(socket);
    runloop = CFRunLoopGetCurrent();
    CFRunLoopAddSource(runloop, source, kCFRunLoopDefaultMode);
    CFRelease(source);
    object->proc = proc;
    object->info = argument;
    object->mask = mask;
    object->source = source;
    return (PyObject*)object;
}

static void
PyEvents_DeleteSocket(PyObject* argument)
{
    if (argument && PyObject_TypeCheck(argument, &SocketType))
    {
        SocketObject* object = (SocketObject*)argument;
        CFRunLoopSourceRef source = object->source;
        CFRunLoopRef runloop = CFRunLoopGetCurrent();
        if (source) {
            CFRunLoopRemoveSource(runloop, source, kCFRunLoopDefaultMode);
            object->source = NULL;
        }
    }
}

static int
PyEvents_HavePendingEvents(void)
{
    return 1;
}

static void
PyEvents_ProcessEvent(void)
{
    NSEvent* event;
    NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
    event = [NSApp nextEventMatchingMask: NSAnyEventMask
                               untilDate: [NSDate distantPast]
                                  inMode: NSDefaultRunLoopMode
                                 dequeue: YES];
    if (event) {
        [NSApp sendEvent: event];
        return;
    }
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,
                       kCFAbsoluteTimeIntervalSince1904,
                       true);
    [pool release];
}

static void
_stdin_callback(CFReadStreamRef stream, CFStreamEventType eventType, void* info)
{
    CFRunLoopRef runloop = info;
    CFRunLoopStop(runloop);
}

static int sigint_fd = -1;

static void _sigint_handler(int sig)
{
    const char c = 'i';
    write(sigint_fd, &c, 1);
}

static void _sigint_callback(CFSocketRef s,
                             CFSocketCallBackType type,
                             CFDataRef address,
                             const void * data,
                             void *info)
{
    char c;
    int* interrupted = info;
    CFSocketNativeHandle handle = CFSocketGetNative(s);
    CFRunLoopRef runloop = CFRunLoopGetCurrent();
    read(handle, &c, 1);
    *interrupted = 1;
    CFRunLoopStop(runloop);
}

static CGEventRef _eventtap_callback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *refcon)
{
    CFRunLoopRef runloop = refcon;
    CFRunLoopStop(runloop);
    return event;
}

static int wait_for_stdin(void)
{
    int i;
    int interrupted = 0;
    const UInt8 buffer[] = "/dev/fd/0";
    const CFIndex n = (CFIndex)strlen((char*)buffer);
    CFRunLoopRef runloop = CFRunLoopGetCurrent();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                                                           buffer,
                                                           n,
                                                           false);
    CFReadStreamRef stream = CFReadStreamCreateWithFile(kCFAllocatorDefault,
                                                        url);
    CFRelease(url);

    CFReadStreamOpen(stream);
#ifdef PYOSINPUTHOOK_REPETITIVE
    if (!CFReadStreamHasBytesAvailable(stream))
    /* This is possible because of how PyOS_InputHook is called from Python */
    {
#endif
        int error;
        int channel[2];
        CFSocketRef sigint_socket = NULL;
        PyOS_sighandler_t py_sigint_handler = NULL;
        CFStreamClientContext clientContext = {0, NULL, NULL, NULL, NULL};
        for (i = 0; i < notifier.nobservers[0]; i++) {
            Observer* observer = notifier.observers[i];
            (*observer)();
        }
        clientContext.info = runloop;
        CFReadStreamSetClient(stream,
                              kCFStreamEventHasBytesAvailable,
                              _stdin_callback,
                              &clientContext);
        CFReadStreamScheduleWithRunLoop(stream, runloop, kCFRunLoopDefaultMode);
        error = socketpair(AF_UNIX, SOCK_STREAM, 0, channel);
        if (error==0)
        {
            CFSocketContext context;
            context.version = 0;
            context.info = &interrupted;
            context.retain = NULL;
            context.release = NULL;
            context.copyDescription = NULL;
            fcntl(channel[0], F_SETFL, O_WRONLY | O_NONBLOCK);
            sigint_socket = CFSocketCreateWithNative(
                kCFAllocatorDefault,
                channel[1],
                kCFSocketReadCallBack,
                _sigint_callback,
                &context);
            if (sigint_socket)
            {
                CFRunLoopSourceRef source;
                source = CFSocketCreateRunLoopSource(kCFAllocatorDefault,
                                                     sigint_socket,
                                                     0);
                CFRelease(sigint_socket);
                if (source)
                {
                    CFRunLoopAddSource(runloop, source, kCFRunLoopDefaultMode);
                    CFRelease(source);
                    sigint_fd = channel[0];
                    py_sigint_handler = PyOS_setsig(SIGINT, _sigint_handler);
                }
            }
        }

        NSEvent* event;
        NSAutoreleasePool* pool = [[NSAutoreleasePool alloc] init];
        while (true) {
            while (true) {
                event = [NSApp nextEventMatchingMask: NSAnyEventMask
                                           untilDate: [NSDate distantPast]
                                              inMode: NSDefaultRunLoopMode
                                             dequeue: YES];
                if (!event) break;
                [NSApp sendEvent: event];
            }
            CFRunLoopRun();
            if (interrupted || CFReadStreamHasBytesAvailable(stream)) break;
        }
        [pool release];
        if (py_sigint_handler) PyOS_setsig(SIGINT, py_sigint_handler);
        CFReadStreamUnscheduleFromRunLoop(stream,
                                          runloop,
                                          kCFRunLoopCommonModes);
        if (sigint_socket) CFSocketInvalidate(sigint_socket);
        if (error==0) {
            close(channel[0]);
            close(channel[1]);
        }
        for (i = 0; i < notifier.nobservers[1]; i++) {
            Observer* observer = notifier.observers[i];
            (*observer)();
        }
#ifdef PYOSINPUTHOOK_REPETITIVE
    }
#endif
    CFReadStreamClose(stream);
    CFRelease(stream);
    if (interrupted) {
        errno = EINTR;
        raise(SIGINT);
        return -1;
    }
    return 1;
}

@implementation WindowServerConnectionManager
static WindowServerConnectionManager *sharedWindowServerConnectionManager = nil;

+ (WindowServerConnectionManager *)sharedManager
{
    if (sharedWindowServerConnectionManager == nil)
    {
        sharedWindowServerConnectionManager = [[super allocWithZone:NULL] init];
    }
    return sharedWindowServerConnectionManager;
}

+ (id)allocWithZone:(NSZone *)zone
{
    return [[self sharedManager] retain];
}

+ (id)copyWithZone:(NSZone *)zone
{
    return self;
}

+ (id)retain
{
    return self;
}

- (NSUInteger)retainCount
{
    return NSUIntegerMax;  //denotes an object that cannot be released
}

- (oneway void)release
{
    // Don't release a singleton object
}

- (id)autorelease
{
    return self;
}

- (void)launch:(NSNotification*)notification
{
    CFRunLoopRef runloop;
    CFMachPortRef port;
    CFRunLoopSourceRef source;
    NSDictionary* dictionary = [notification userInfo];
    NSNumber* psnLow = [dictionary valueForKey: @"NSApplicationProcessSerialNumberLow"];
    NSNumber* psnHigh = [dictionary valueForKey: @"NSApplicationProcessSerialNumberHigh"];
    ProcessSerialNumber psn;
    psn.highLongOfPSN = [psnHigh intValue];
    psn.lowLongOfPSN = [psnLow intValue];
    runloop = CFRunLoopGetCurrent();
    port = CGEventTapCreateForPSN(&psn,
                                  kCGHeadInsertEventTap,
                                  kCGEventTapOptionListenOnly,
                                  kCGEventMaskForAllEvents,
                                  &_eventtap_callback,
                                  runloop);
    source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault,
                                           port,
                                           0);
    CFRunLoopAddSource(runloop, source, kCFRunLoopDefaultMode);
    CFRelease(port);
}
@end

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

    WindowServerConnectionManager* connectionManager = [WindowServerConnectionManager sharedManager];
    NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
    NSNotificationCenter* notificationCenter = [workspace notificationCenter];
    [notificationCenter addObserver: connectionManager
                           selector: @selector(launch:)
                               name: NSWorkspaceDidLaunchApplicationNotification
                             object: nil];

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
    notifier.observers[0] = NULL;
    notifier.observers[1] = NULL;
    notifier.nobservers[0] = 0;
    notifier.nobservers[1] = 0;
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
