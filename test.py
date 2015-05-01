import sys
import events
import events_tkinter
events_tkinter.add_timer = events.add_timer
events_tkinter.remove_timer = events.remove_timer
events_tkinter.create_socket = events.create_socket
events_tkinter.delete_socket = events.delete_socket
events_tkinter.wait_for_event = events.wait_for_event

if sys.version_info[0] < 3:
    from Tkinter import *
else:
    from tkinter import *

def f(timer):
    print 'hello1'
    events.add_timer(1000, f)

def g(timer):
    print 'hello2'
    events.add_timer(5000, g)

f(None)
g(None)

t = Tk()
t.eval('proc f {} { puts "hello"; after 1000 f }; f;')
b = Button(t, text="Hello, world!")
b.pack()

# t.evalfile('widget')
