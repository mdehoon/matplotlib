import sys
import events
import events_tkinter
events_tkinter.load()

if sys.version_info[0] < 3:
    from Tkinter import *
else:
    from tkinter import *

t = Tk()
t.eval('proc f {} { puts "hello"; after 1000 f }; f;')
t.evalfile('widget')
