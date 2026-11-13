' Start TeleCine Audio Translator zonder zwart opdrachtvenster.
Option Explicit
Dim fso, shell, dir, pyw, script, log, cmd
Set fso = CreateObject("Scripting.FileSystemObject")
Set shell = CreateObject("WScript.Shell")
dir = fso.GetParentFolderName(WScript.ScriptFullName)
pyw = dir & "\.venv\Scripts\pythonw.exe"
script = dir & "\TeleCine_AudioTranslator.py"
log = dir & "\telecine-start.log"

If Not fso.FileExists(pyw) Then
  MsgBox "De omgeving is nog niet geinstalleerd." & vbCrLf & vbCrLf & _
         "Dubbelklik eerst op installeer.bat in deze map.", vbCritical, "TeleCine Audio Translator"
  WScript.Quit 1
End If

shell.CurrentDirectory = dir
' Verborgen starten. Fouten komen in telecine-start.log.
cmd = "cmd /c """"" & pyw & """ """ & script & """ >> """ & log & """ 2>&1"""
shell.Run cmd, 0, False
