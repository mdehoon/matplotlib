#include <tcl.h>
#include <tk.h>

int main(int argc, char *argv[])
{ 
    Tcl_Interp *interp; 
    interp = Tcl_CreateInterp(); 
    if (Tcl_Init(interp) != TCL_OK) { 
        return 1; 
    } 
    Tk_Init(interp);
    if (argc==2) {
        Tcl_EvalFile(interp, argv[1]); 
        while (1) {
            Tcl_DoOneEvent(0);
        }
    }
    Tcl_Finalize();
    return 0;
} 
