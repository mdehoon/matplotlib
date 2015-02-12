#include <Python.h>

#if PY_MAJOR_VERSION >= 3
#define PY3K 1
#else
#define PY3K 0
#endif

static struct PyMethodDef methods[] = {
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
#if PY3K
    return module;
#endif
}
