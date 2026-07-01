' Clawd Mochi helper — double-click to start the tray app (no console window).
' Not autostart: just a convenient silent launcher.
' The crab's COM port is auto-detected (no need to edit anything).
Set sh = CreateObject("WScript.Shell")
pyw = "C:\Users\a.nozhenko\AppData\Local\Programs\Python\Python310\pythonw.exe"
script = "d:\Home\ClawdMochi\pc-helper\helper.py"
sh.Run """" & pyw & """ """ & script & """", 0, False
