import sys
import Tkinter as Tk
import events
import events_tkinter
events_tkinter.add_timer = events.add_timer
events_tkinter.remove_timer = events.remove_timer
events_tkinter.create_socket = events.create_socket
events_tkinter.delete_socket = events.delete_socket
events_tkinter.wait_for_event = events.wait_for_event


window = Tk.Tk()
window.withdraw()

icon_fname = "lib/matplotlib/mpl-data/images/matplotlib.gif"
print "Calling Tk.PhotoImage with file", icon_fname
sys.stdout.flush()
icon_img = Tk.PhotoImage(file=icon_fname)
print "After calling Tk.PhotoImage"
sys.stdout.flush()
