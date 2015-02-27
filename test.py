import events
import events_tkinter
events_tkinter.load()
events.start()

from Tkinter import *

t = Tk()
t.eval('proc f {} { puts "hello"; after 1000 f }; f;')
t.evalfile('widget')
