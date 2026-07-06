Option Explicit
Dim Shell
Dim SEC
Dim Width
If WScript.Arguments.count > 1 Then
SEC = WScript.Arguments(1)
Else SEC = 1
End If
Set Shell = WScript.CreateObject("WScript.Shell")
If (WScript.Arguments.count > 0) then
	Shell.popup "You Program Installer: " & WScript.Arguments(0) , SEC, "You Program Installer", 64
Else	Shell.popup "You Program Installer: Running ...", SEC, "You Program Installer", 64
End If
Set Shell = Nothing
