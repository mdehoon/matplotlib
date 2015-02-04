#include <Python.h>
#include <tcl.h>
#include <tk.h>

#if PY_MAJOR_VERSION >= 3
#define PY3K 1
#else
#define PY3K 0
#endif

static PyObject*
run(PyObject* unused, PyObject* args)
{
    const char* filename;
    if(!PyArg_ParseTuple(args, "s", &filename))
        return NULL;
    Tcl_Interp *interp; 
    interp = Tcl_CreateInterp(); 
    if (Tcl_Init(interp) != TCL_OK) { 
        PyErr_SetString(PyExc_RuntimeError, "Failed to initialize Tcl");
        return NULL;
    } 
    Tk_Init(interp);
    Tcl_EvalFile(interp, filename); 
    while (1) {
        Tcl_DoOneEvent(0);
    }
    Tcl_Finalize();
    Py_INCREF(Py_None);
    return Py_None;
}

static struct PyMethodDef methods[] = {
   {"run",
    (PyCFunction)run,
    METH_VARARGS,
    "run a Tcl script."
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
#if PY3K
    return module;
#endif
}
