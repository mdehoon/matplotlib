#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <Cocoa/Cocoa.h>
#include <Python.h>

#if PY_MAJOR_VERSION >= 3
#define PY3K 1
#else
#if PY_MINOR_VERSION < 7
#error Python version should be 2.7 or newer
#else
#define PY3K 0
#endif
#endif

void run(void)
{
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.e-6, false);
}

static struct PyMethodDef methods[] = {
   {NULL,          NULL, 0, NULL} /* sentinel */
};

#if PY3K
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

void initevents_macosx(void)
#endif
{
    PyObject *module;
#if PY3K
    module = PyModule_Create(&moduledef);
#else
    module = Py_InitModule4("events_macosx",
                            methods,
                            "events module",
                            NULL,
                            PYTHON_API_VERSION);
#endif
    if (module==NULL) goto error;
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
