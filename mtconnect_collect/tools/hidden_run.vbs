' hidden_run.vbs - run a command line with the console window hidden
' usage: wscript hidden_run.vbs "<command line>"
Set sh = CreateObject("WScript.Shell")
sh.Run """" & WScript.Arguments(0) & """", 0, False
