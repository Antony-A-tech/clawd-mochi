' Clawd Mochi helper — double-click to start the tray app (no console window).
' Portable: runs pythonw (from PATH) on helper.py next to this file. Not autostart.
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh  = CreateObject("WScript.Shell")
here = fso.GetParentFolderName(WScript.ScriptFullName)
sh.CurrentDirectory = here
sh.Run "pythonw """ & here & "\helper.py""", 0, False
