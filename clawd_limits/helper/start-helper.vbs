' Clawd Mochi helper — double-click to start the tray app (no console window).
' Not autostart: just a convenient silent launcher. Edit COM6 if your port differs.
Set sh = CreateObject("WScript.Shell")
pyw = "C:\Users\a.nozhenko\AppData\Local\Programs\Python\Python310\pythonw.exe"
script = "d:\Home\ClawdMochi\pc-helper\helper.py"
sh.Run """" & pyw & """ """ & script & """ COM6", 0, False
