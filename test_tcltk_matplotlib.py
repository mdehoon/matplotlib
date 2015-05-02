import sys
import events
import events_tkinter
events_tkinter.add_timer = events.add_timer
events_tkinter.remove_timer = events.remove_timer
events_tkinter.create_socket = events.create_socket
events_tkinter.delete_socket = events.delete_socket
events_tkinter.wait_for_event = events.wait_for_event

import Tkinter

def f():
    print 'hello'

root = Tkinter.Tk()
b = Tkinter.Button(root, text="Hello, world!", command=f)
b.pack()

# import matplotlib
# matplotlib.use("tkagg")
# from pylab import *
