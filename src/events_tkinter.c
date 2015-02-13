#include <Python.h>
#include "events.h"

#if PY_MAJOR_VERSION >= 3
#define PY3K 1
#else
#define PY3K 0
#endif

static PyObject *
events_system(PyObject *self, PyObject *args)
{
    const char *command;
    int sts;

    if (!PyArg_ParseTuple(args, "s", &command))
        return NULL;
    sts = PyEvents_System(command);
    return Py_BuildValue("i", sts);
}

static struct PyMethodDef methods[] = {
   {"system",
    (PyCFunction)events_system,
    METH_VARARGS,
    "system call"
   },
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
    if (import_events() < 0)
#if PY3K
        return NULL;
#else
        return;
#endif
    
#if PY3K
    return module;
#endif
}
