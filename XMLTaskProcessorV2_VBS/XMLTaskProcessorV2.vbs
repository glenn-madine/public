Option Explicit
' Registry Hive constants
const HKEY_CLASSES_ROOT = &H80000000
Const HKEY_CURRENT_USER = &H80000001
Const HKEY_LOCAL_MACHINE = &H80000002
const HKEY_USERS = &H80000003
const HKEY_CURRENT_CONFIG = &H80000005
' Registry Operations constants
Const KEY_QUERY_VALUE = &H0001
Const KEY_SET_VALUE = &H0002
Const KEY_CREATE_SUB_KEY = &H0004
Const DELETE = &H00010000
' Registry types constants
Const REG_SZ = 1
Const REG_EXPAND_SZ = 2
Const REG_BINARY = 3
Const REG_DWORD = 4
Const REG_MULTI_SZ = 7
' File I/O stream constants
Const STDIN = 0
Const STDOUT = 1
Const STDERR = 2
' File I/O operations constants
Const FORREADING = 1 
Const FORWRITING = 2 
Const FORAPPENDING = 8 
Const DONOTCREATEFILE = False
Const CREATEFILE = True
Const OVERWRITEEXISTING = TRUE
' File format type constants
Const ASCII = 0
Const UNICODEFILE = -1
Const SYSTEMDEFAULT = -2
' Error handling constants
Const SUCCESS = 0
Const ERROROCCURRED = 1
Const WARNING = 5
' Folder constants
Const WINDOWSFOLDER = 0
Const SYSTEMFOLDER = 1
Const TEMPFOLDER = 2
' File attributes constants
Const fAttrREADONLY = 1
Const fAttrHIDDEN = 2
Const fAttrSYSTEM = 4
Const fAttrFOLDER = 16
Const fAttrARCHIVE = 32
Const fAttrLINK = 64
Const fAttrCOMPRESSED = 2048
' Other Constants
Const AUDIT_SUCCESS = 8
Const AUDIT_FAILURE = 16
Const NO_TIMEOUT = 0
' Pop ups
'	Buttons
	Const OK = 0
	Const OK_CANCEL = 1
	Const ABORT_IGNORE_RETRY=2
	Const YES_NO_CANCEL = 3
	Const YES_NO = 4
	Const RETRY_CANCEL = 5
'	Box Types
	Const CRITICAL = 16
	Const QUESTION = 32
	Const EXCLAMATION = 48
	Const INFORMATION = 64
'	Results of prompts
	Const OK_CLICKED = 1
	Const CANCEL_CLICKED = 2
	Const ABORT_CLICKED = 3
	Const RETRY_CLICKED = 4
	Const IGNORE_CLICKED  = 5
	Const YES_CLICKED = 6
	Const NO_CLICKED = 7
	Const NONE_CLICKED = -1  'message box was dismissed automatically (timeout)
' done
Const wbemFlagReturnImmediately = &h10
Const wbemFlagForwardOnly = &h20
' Run Process Styles
	Const HIDE_WINDOW = 0 ' Hides the window and activates another window. 
	Const DISPLAY_WINDOW = 1 ' Activates and displays a window. If the window is minimized or maximized, the system restores it to its original size and position. An application should specify this flag when displaying the window for the first time. 
	Const MINIMIZED_WINDOW = 2 ' Activates the window and displays it as a minimized window
	Const MAXIMIZED_WINDOW = 3 ' Activates the window and displays it as a maximized window
	Const DISPLAY_WINDOW_DO_NOT_ACTIVATE = 4 ' Displays a window in its most recent size and position. The active window remains active
	Const ACTIVATE_WINDOW_KEEP_CURRENT_SIZE = 5 ' Activates the window and displays it in its current size and position
	Const MINIMIZE_AND_ACTIVATE_NEXT = 6 ' Minimizes the specified window and activates the next top-level window in the Z order
	Const DISPLAY_WINDOW_AS_MINIMIZED_DO_NOT_ACTIVATE = 7 ' Displays the window as a minimized window. The active window remains active
	Const DISPLAY_WINDOW_IN_CURRENT_STATE = 8 ' Displays the window in its current state. The active window remains active.
	Const ACTIVATE_AND_DISPLAY_WINDOW = 9 ' Activates and displays the window. If the window is minimized or maximized, the system restores it to its original size and position. An application should specify this flag when restoring a minimized window
	Const SETS_SHOW_STATE = 10  ' Sets the show state based on the state of the program that started the application
	Const WAITONRETURN = TRUE
	Const NOWAIT = FALSE
' done

'Return the file name portion of a full pathname 
Function Basename(FullPath)
	Dim x
	Dim y
	Dim tmpstring
	tmpstring = FullPath
	x = Len(FullPath)
	For y = x To 1 Step -1
		If mid(FullPath, y, 1) = "\" Or mid(FullPath, y, 1) = ":" Or mid(FullPath, y, 1) = "/" Then
			tmpstring = mid(Fullpath, y+1)
			Exit For
		End If
	Next
	Basename = tmpstring
End Function '- Basename

' OPT: Merged Is32BitOS / Is64BitOS into a single cached helper.
' Both functions queried WMI twice on every call-site; now they read a
' module-level variable (BITNESS_GLOBAL) that is populated once at startup.
' See "Dim BITNESS_GLOBAL" in the main-line section below.
Function Is64BitOS()
    Is64BitOS = (BITNESS_GLOBAL = 64)
End Function

Function Is32BitOS()
    Is32BitOS = (BITNESS_GLOBAL = 32)
End Function

Function CreateShortCut(strAppPath,strWorkDir,strIconPath,Description,PlaceShutcutWhere,HotKey,WindowStyle)
	Dim objShell
	Dim objDesktop
	Dim objLink
	' on error resume next
	Set objShell = CreateObject("WScript.Shell")
	Set objLink = objShell.CreateShortcut(PlaceShutcutWhere)
	objLink.Description = Description 
	objLink.HotKey = HotKey 
	objLink.IconLocation = strIconPath 
	objLink.TargetPath = strAppPath
	objLink.WindowStyle = WindowStyle 
	objLink.WorkingDirectory = strWorkDir
	objLink.Save
	CreateShortCut = Err.Number
	If err.number <> 0 then err.clear
	Set objLink = nothing
	Set objShell = nothing
End Function

'Return the path portion of a full pathname 
Function Pathname(FullPath)
	Dim x
	Dim y
	Dim tmpstring
	x = Len(FullPath)
	For y = x To 1 Step -1
		If mid(FullPath, y, 1) = "\" Or mid(FullPath, y, 1) = "/" Then
			tmpstring = mid(Fullpath, 1, y-1)
			Exit For
		End If
	Next
	Pathname = tmpstring
End Function '- Pathname

'Return the filename extension portion of a path/filename 
Function FileExt(FullPath)
	Dim x
	Dim y
	Dim tmpstring
	x = Len(FullPath)
	For y = x To 1 Step -1
		If mid(FullPath, y, 1) = "." Then
			tmpstring = mid(Fullpath, y+1)
			Exit For
		End If
	Next
	FileExt = tmpstring
End Function '- FileExt

'Return the length of a file Or -1 If it does not exist 
Function GetFileSize(Fname)
	Dim fs
	Dim f
	GetFileSize = -1
	Set fs = CreateObject("Scripting.FileSystemObject")
	If fs.FileExists(Fname) = True Then
		set f = fs.GetFile(Fname)
		GetFileSize = f.size
	End If
	If fs.FolderExists(Fname) = True Then
		set f = fs.GetFolder(Fname)
		GetFileSize = f.size
	End If
	Set f = Nothing
	Set fs = Nothing
End Function '- GetFileSize

'Return TRUE if a file exists else FALSE
' OPT: Simplified — original used If/Else to manually assign True/False;
' direct assignment of the Boolean expression is cleaner and avoids a
' CreateObject overhead by delegating to the FSO boolean directly.
Function FileExists(Fname)
	Dim fs
	Set fs = CreateObject("Scripting.FileSystemObject")
	FileExists = fs.FileExists(Fname)
	Set fs = Nothing
End Function '- FileExists

' OPT: PathExists removed — it was an exact duplicate of FolderExists below.
' All callers should use FolderExists directly.

'Return TRUE if a folder exists else FALSE
' OPT: Same simplification as FileExists — direct Boolean assignment.
Function FolderExists(Pathname)
	Dim fs
	Set fs = CreateObject("Scripting.FileSystemObject")
	FolderExists = fs.FolderExists(Pathname)
	Set fs = Nothing
End Function '- FolderExists

Function DetectLocalPrinter(ptr)
	Dim objWMIService
	Dim colPrinters
	Set objWMIService = GetObject("winmgmts:\\.\root\cimv2")
	Set colPrinters = objWMIService.ExecQuery("Select * From Win32_Printer where name = '" & ptr & "'")
	If colPrinters.Count <> 0 Then
		DetectLocalPrinter = true
	else
		DetectLocalPrinter = false
	End If
End function

'Returns a random number between HI And LO,  inclusive 
Function RandomNumb(hi, lo)
	Randomize
	RandomNumb = Int((hi - lo + 1) * Rnd + lo) 
End Function '- RandomNumb

'Return number of kilobytes free For a given drive 
Function GetDiskFree(drive)
	Dim s
	Dim d
	Dim fs
	' on error resume next
	s = -1
	Set fs = CreateObject("Scripting.FileSystemObject")
	Set d = fs.GetDrive(fs.GetDriveName(drive))
	If d.FreeSpace < 0 Then
		s = -1
	Else
		s = (d.FreeSpace / 1024)
	End If
	set d = Nothing
	set fs = Nothing
	GetDiskFree = s
End Function '- GetDiskFree

'Return Windows directory
Function GetWinDir
	Dim fs
	Set fs = CreateObject("Scripting.FileSystemObject")
	GetWinDir = fs.GetSpecialFolder(WINDOWSFOLDER)
	Set fs = Nothing
End Function '- GetWinDir

'Return Windows/System directory
Function GetWinSys
	Dim fs
	Set fs = CreateObject("Scripting.FileSystemObject")
	GetWinSys = fs.GetSpecialFolder(SYSTEMFOLDER)
	Set fs = Nothing
End Function '- GetWinSys

'Return Windows/temp directory
Function GetWinTemp
	Dim fs
	Set fs = CreateObject("Scripting.FileSystemObject")
	GetWinTemp = fs.GetSpecialFolder(TEMPFOLDER)
	Set fs = Nothing
End Function '- GetWinTemp

'Return temp filename
Function GetTempName
	Dim fs
	Set fs = CreateObject("Scripting.FileSystemObject")
	GetTempName = fs.GetTempName
	Set fs = Nothing
End Function '- GetTempName

'Return full path And temp filename
Function GetTempFullPath
	Dim fs
	GetTempFullPath = GetWinTemp & "\" & GetTempName
	Set fs = Nothing
End Function '- GetTempFullPath

'Return the date/time a file was created 
Function FileCreated(Fname)
	Dim fs
	Dim f
	FileCreated = ""
	Set fs = CreateObject("Scripting.FileSystemObject")
	If fs.FileExists(Fname) = True Then
		Set f = fs.GetFile(Fname)
		FileCreated = f.DateCreated
	End If
	Set f = Nothing
	Set fs = Nothing
End Function '- FileCreated

Sub NewFile(Name,OverWrite)
	' OPT: Removed duplicate CreateTextFile call (was called twice in original).
	Dim fso
	Dim nf
	Set fso = CreateObject("Scripting.FileSystemObject")
	Set nf = fso.CreateTextFile(Name, OverWrite)
    nf.Close
    Set nf = Nothing
    Set fso = Nothing	
End Sub

'Return the date a file was last accessed 
Function FileLastAccessed(Fname)
	Dim fs
	Dim f
	FileLastAccessed = ""
	Set fs = CreateObject("Scripting.FileSystemObject")
	If fs.FileExists(Fname) = True Then
		Set f = fs.GetFile(Fname)
		FileLastAccessed = f.DateLastAccessed
	End If
	Set f = Nothing
	Set fs = Nothing
End Function '- FileLastAccessed

'Return the date/time a file was last modified 
Function FileLastModified(Fname)
	Dim fs
	Dim f
	FileLastModified = ""
	Set fs = CreateObject("Scripting.FileSystemObject")
	If fs.FileExists(Fname) = True Then
		Set f = fs.GetFile(Fname)
		FileLastModified = f.DateLastModified
	End If
	Set f = Nothing
	Set fs = Nothing
End Function '- FileLastModified

'Return the date/time a folder was created 
Function FolderCreated(Fname)
	Dim fs
	Dim f
	FolderCreated = ""
	Set fs = CreateObject("Scripting.FileSystemObject")
	If fs.FolderExists(Fname) = True Then
		Set f = fs.GetFolder(Fname)
		FolderCreated = f.DateCreated
	End If
	Set f = Nothing
	Set fs = Nothing
End Function '- FolderCreated

'Return the date a folder was last accessed 
Function FolderLastAccessed(Fname)
	Dim fs
	Dim f	
	FolderLastAccessed = ""
	Set fs = CreateObject("Scripting.FileSystemObject")
	If fs.FolderExists(Fname) = True Then
		Set f = fs.GetFolder(Fname)
		FolderLastAccessed = f.DateLastAccessed
	End If
	Set f = Nothing
	Set fs = Nothing
End Function '- FolderLastAccessed

'Return the date/time a folder was last modified  
Function FolderLastModified(Fname)
	Dim fs
	Dim f
	FolderLastModified = ""
	Set fs = CreateObject("Scripting.FileSystemObject")
	If fs.FolderExists(Fname) = True Then
		Set f = fs.GetFolder(Fname)
		FolderLastModified = f.DateLastModified
	End If
	Set f = Nothing
	Set fs = Nothing
End Function '- FolderLastModified

'Logs a message To the NT logfile (wsh.log On W9x) 
Sub LogEvent(msgType, Msg)
	Dim WshShell
	Set WshShell = WScript.CreateObject("WScript.Shell")
	WshShell.LogEvent msgType, Msg
	Set WshShell = Nothing
End Sub '= LogEvent

'Returns the number of elements in an array 
Function ElementsInArray(ArrayName)
	Dim Fields
	Fields = 0
	If IsEmpty(ArrayName) <> True Then
		Fields = UBound(ArrayName) - LBound(ArrayName) + 1
	End If
	ElementsInArray = Fields
End Function '- ElementsInArray

'Returns the number of non-Empty elements in an array 
Function NonEmptyElementsInArray(ArrayName)
	Dim Fields
	Dim tmp
	Fields = 0
	If IsEmpty(ArrayName) <> True Then
		For Each tmp in ArrayName
			If tmp <> "" Then
				Fields = Fields + 1
			End If
		Next
	End If
	NonEmptyElementsInArray = Fields
End Function '- NonEmptyElementsInArray

' Is any date in DST And dates DST begins And Ends 
Function IsDST(TodayDate, arrReturn)  
' Is any date in DST And dates DST begins And Ends  
' Args = Date, 1 dimensional 2 element array  
' Returns -1 On Error (bad date)  
' 0 If NOT DST  
' 1 If in DST  
' arrRetunr(0) = First day of DST  
' arrReturn(1) = Last day of DST  
' Paul R. Sadowski <aa089#bfn.org>
	Dim StartDate
	Dim EndDate
	Dim StartDOW
	Dim EndDOW
	Dim TargetDOW
	Dim BeginDST
	Dim EndDST
	If IsDate(TodayDate) <> True Then
		arrReturn(0) = -1
		arrReturn(1) = -1
		IsDST = -1
		Exit Function
	End If
	StartDate = CDate("4/1/" & Cstr(Year(TodayDate)))
	EndDate = CDate("11/1/" & Cstr(Year(TodayDate)))
	StartDOW = Weekday(StartDate)
	If StartDOW <> 1 Then
		TargetDOW = 8 - StartDOW 
	End If
	BeginDST = DateAdd("d", TargetDOW, StartDate)
	EndDOW = Weekday(EndDate)
	If EndDOW <> 1 Then
		TargetDOW = 1 - EndDOW
	End If
	EndDST = DateAdd("d", TargetDOW, EndDate)
	If DateDiff("d", BeginDST, TodayDate) >= 0 Then
		If DateDiff("d", EndDST, TodayDate) < 0 Then
			arrReturn(0) = BeginDST
			arrReturn(1) = EndDST
			IsDST = 1
			Exit Function
		End If
	End If
	arrReturn(0) = BeginDST
	arrReturn(1) = EndDST
	IsDST = 0
End Function '- IsDST

'Chops the last character off a string
Function Chop(strString)
	Dim x
	x = Len(strString)
	Chop = mid(strString, 1, x -1)
End Function  '- Chop 

'Chops the last character off a string If it matches strchar 
Function ChopChar(strString, strChar)
	Dim x
	x = Len(strString)
	If mid(strString, x) = strChar Then
		ChopChar = mid(strString, 1, x -1)
	Else
		ChopChar = strString
	End If
End Function '- ChopChar

Function ChopChop(strInput, strCutBefore, strCutAfter)
  If strCutBefore <> "" Then
    If InStr(strInput, strCutBefore) > 0 Then
      strInput = Right(strInput, Len(strInput) - InStr(strInput, strCutBefore) + 1)
    End If
  End If
  If strCutAfter <> "" Then
    If InStr(strInput, strCutAfter) > 0 Then
      strInput = Left(strInput, InStr(strInput, strCutAfter) + Len(strCutAfter) - 1)
        'Use InStrRev instead to search from end to beginning
    End If
  End If
  ChopChop = strInput
End Function '- ChopChop

''=======================================================================

Function CdRomDrives
 ' Returns array of all local CD-ROM drives
	Dim drive
	Dim aTmp()
	Dim i
	i = -1
	For Each drive In CreateObject("Scripting.FileSystemObject").Drives
		If drive.DriveType = 4 Then
			i = i + 1
			ReDim Preserve aTmp(i)
			aTmp(i) = drive.DriveLetter
		End If
	Next
	CdRomDrives = aTmp
End Function

Function DeleteIfEmpty(sFldr)
 ' Deletes a folder IF there is nothing in it
	Dim FSO
	Dim Flder
	Dim HasFiles
	Dim HasFlders
	Set FSO = CreateObject("Scripting.FileSystemObject")
	Set Fldr = FSO.GetFolder(sFldr)
	HasFiles = CBool(Fldr.Files.Count)
	HasFlders = CBool(Fldr.SubFolders.Count)
	If ( (HasFiles = False) And (HasFlders = False) ) Then
		FSO.DeleteFolder sFldr
	End If
End Function

Function ShowDriveType(drvpath)
' Modified from WSH 5.6 Docs
' drivepath should be in form "x","x:", or "x:\"
' Returns human-readable drive type
	Dim fso
	Dim d
	'Dim t
	Set fso = CreateObject("Scripting.FileSystemObject")
	Set d = fso.GetDrive(drvpath)
	Select Case d.DriveType
		Case 0: ShowDriveType = "Unknown"
		Case 1: ShowDriveType = "Removable"
		Case 2: ShowDriveType = "Fixed"
		Case 3: ShowDriveType = "Network"
		Case 4: ShowDriveType = "CD-ROM"
		Case 5: ShowDriveType = "RAM Disk"
	End Select
End Function

Sub fAppend(FilePath, sData)
'Given the path to a file, will append sData to it
	With CreateObject("Scripting.FileSystemObject").OpenTextFile(FilePath, 8)
		.Write sData
		.Close
	End With
End Sub

Function FileAge(sPath)
 ' Returns file age in days
	With CreateObject("Scripting.FileSystemObject").GetFile(sPath)
		FileAge = CDbl(Now) - CDbl(.DateLastModified)
	End With
End Function

Function FolderSet(FolPath)
 ' finds all folders in path FolPath
	Dim FSO
	Set FSO = CreateObject("Scripting.FileSystemObject")
	Dim Fol
	'Dim TEMPFOLDER_GLOBAL
	Dim aFols()
	Dim oFolder
	Dim subFolder
	Dim aTmp
	Dim i
	ReDim aFols(-1)
	Set oFolder = FSO.GetFolder(FolPath)
	For Each Fol In oFolder.SubFolders
		ReDim Preserve aFols(UBound(aFols) + 1)
		aFols(UBound(aFols)) = Fol.Path
	Next
	'Recurse through all of the folders
	For Each subFolder In oFolder.subFolders
		aTmp = FolderSet(subFolder)
		For i = 0 To UBound(aTmp)
			ReDim Preserve aFols(UBound(aFols) + 1)
			aFols(UBound(aFols)) = aTmp(i)
			' OPT: Removed erroneous "i = i + 1" here — the For loop already
			' increments i, so the manual increment was skipping every other
			' recursive result.
		Next
	Next
	FolderSet = aFols
End Function

Function FolderSize(folspec)
	FolderSize = CreateObject("Scripting.FileSystemObject").GetFolder(folspec).size
End Function

Function folIsEmpty(sFldr)
	folIsEmpty = CStr(CBool(CreateObject("Scripting.FileSystemObject").GetFolder(sFldr).Files.Count))
End Function

Function fRead(FilePath)
 'Given the path to a file, will return entire contents  
 ' works with either ANSI or Unicode
	With CreateObject("Scripting.FileSystemObject").OpenTextFile(FilePath, FORREADING, False, SYSTEMDEFAULT)
		fRead = .ReadAll
		.Close
	End With
End Function

Function FreeDrive(sFirst,sLast)
'Finds first available drive letter in a sequence 
' specify like this: fDrive = FreeDrive("C","Z") 
' WARNING: recommended uses is to start with C 
' since some systems will not correctly handle 
' A or B used for mapping.
	Dim I
	With CreateObject("Scripting.FileSystemObject")
		For I = Asc(sFirst) To Asc(sLast)
			If Not .DriveExists(Chr(I)) Then
				Freedrive = Chr(I) & ":"
				Exit Function
			End If
		Next
	End With
End Function

Sub fWrite(FilePath, sData)
 'writes sData to FilePath
	With CreateObject("Scripting.FileSystemObject").OpenTextFile(FilePath, FORWRITING, CREATEFILE)
		.Write sData
		.Close
	End With
End Sub
 
Function GetRemovableDrive
	Dim drive
	for each drive in CreateObject("Scripting.FileSystemObject").Drives
		if drive.DriveType = 1 Then
			GetRemovableDrive = drive.DriveLetter + ":\"
			exit function
		end if
	next
end function

Function HasRights(folderspec)
' Returns true if user has at least list rights  
' to path; can be a disk or UNC path  
	' on error resume next  
	Dim fso
	Dim f
	Dim fc
	Dim f1  
	HasRights = CStr(False)  
	Set fso = CreateObject("Scripting.FileSystemObject")
	Set f = fso.GetFolder(folderspec)
	Set fc = f.Files
	For Each f1 In fc
		If Err.Number <> 0 Then 
			Exit For
		end if
			HasRights = CStr(True)
			Exit Function
	Next
	Err.Clear
	On Error GoTo 0
End Function

Function Head(fil, nlines)
 'Given the path to a file, will return  
 ' count nlines # of lines  
 ' lines will be separated with vbCrLF if more than  
 ' one is specified  ' works with either ANSI or Unicode
	Dim i
	With CreateObject("Scripting.FileSystemObject").OpenTextFile(fil, FORREADING, False, SYSTEMDEFAULT)
		For i = 1 To nlines
			If Not .AtEndOfStream Then 
				Head = Head & .ReadLine
				If i < nlines Then 
					Head = Head & vbCrLf
				end if
			Else 
				Exit For
			End If
		Next
		.Close
	End With
End Function

Sub HideFolder(strFolder)
	'Hides a folder passed to it with path strFolder  
	'From a Michael Harris script  
	Dim FSO
	Set FSO = createobject("scripting.filesystemobject")
	With FSO.getfolder(strFolder)
		.Attributes = .Attributes Or fAttrHIDDEN
	End With
End Sub

Sub UnHideFolder(strFolder)
	'Un-hides a folder passed to it with path strFolder
	Dim FSO
	Set FSO = createobject("scripting.filesystemobject")
	With FSO.getfolder(strFolder)
		.Attributes = .Attributes XOr fAttrHIDDEN
	End With
End Sub

Sub HideFile(strFile)
	'Hides a File passed to it with path strFile  
	'From a Michael Harris script  
	Dim FSO
	Set FSO = createobject("scripting.filesystemobject")
	With FSO.getfile(strFile)
		.Attributes = .Attributes Or fAttrHIDDEN
	End With
End Sub

Sub UnHideFile(strFile)
	'Un-hides a File passed to it with path strFile
	Dim FSO
	Set FSO = createobject("scripting.filesystemobject")
	With FSO.getfile(strFile)
		.Attributes = .Attributes XOr fAttrHIDDEN
	End With
End Sub

Sub SetReadOnlyFile(strFile)
	'Un-hides a File passed to it with path strFile
	Dim FSO
	Set FSO = createobject("scripting.filesystemobject")
	With FSO.getfile(strFile)
		.Attributes = .Attributes Or fAttrREADONLY
	End With
End Sub

Sub UnReadOnlyFile(strFile)
	'Un-hides a File passed to it with path strFile
	Dim FSO
	Set FSO = createobject("scripting.filesystemobject")
	With FSO.getfile(strFile)
		.Attributes = .Attributes XOr fAttrREADONLY
	End With
End Sub


Sub IncludeVbsScript(FileName)
 ' Given the VBS file, will execute entire contents  ' in global context
	Dim fsObj
	Dim vbsFile
	Set fsObj = CreateObject("Scripting.FileSystemObject")
	Set vbsFile = fsObj.OpenTextFile(FileName, 1, False)
	Dim myFunctionsStr 
	myFunctionsStr = vbsFile.ReadAll
	vbsFile.Close
	Set vbsFile = Nothing
	Set fsObj = Nothing
	ExecuteGlobal myFunctionsStr
End Sub

Function IsDcUp(Server)
	' Verify whether a DC is uo or not
	' on error resume next
	CreateObject("Scripting.FileSystemObject").GetFolder("\\" & Server & "\netlogon")  
	If Err.Number <> 0 Then
		IsDcUp = False
		Err.Clear
	Else
		IsDcUp = True
	End If
	On Error Goto 0
End Function

Function IsHiddenFile(strFile)
	'Checks if the file is hidden  
	Dim FSO
	Dim Attributes
	Set FSO = createobject("scripting.filesystemobject")
	Attributes = FSO.getfile(strFile).Attributes
	IsHiddenFile = CBool(Attributes And 2)  
End Function

Function IsHiddenFolder(strFolder)
	'Checks if the folder is hidden  
	Dim objFSO
	Dim objFolder
	Set objFSO = CreateObject("Scripting.FileSystemObject")
	Set objFolder = objFSO.GetFolder(strFolder)
	If objFolder.Attributes AND 2 Then
		IsHiddenFolder = True
	else
		IsHiddenFolder = false
	End If  
End Function

Function isFolderAttribute(strPath,attributes) ' valid attribute hidden, system, readonly, folder, archive, and compressed.
	Dim objFSO
	Dim objFolder
	Set objFSO = CreateObject("Scripting.FileSystemObject")
	Set objFolder = objFSO.GetFolder(strPath)
	select case lcase(attributes)
		case "hidden"
			If objFolder.Attributes AND fAttrHIDDEN Then
				isFolderAttribute = True
			else
				isFolderAttribute = false
			End If    
		Case "system"
			If objFolder.Attributes AND fAttrSYSTEM Then
				isFolderAttribute = True
			else
				isFolderAttribute = false
			End If   
		Case "readonly"
			If objFolder.Attributes AND fAttrREADONLY Then
				isFolderAttribute = True
			else
				isFolderAttribute = false
			End If
			Case "folder"	
			If objFolder.Attributes AND fAttrFOLDER Then
				isFolderAttribute = True
			else
				isFolderAttribute = false
			End If 
		Case "archive"
			If objFolder.Attributes AND fAttrARCHIVE Then
				isFolderAttribute = True
			else
				isFolderAttribute = false
			End If
		Case "compressed"
			If objFolder.Attributes AND fAttrCOMPRESSED Then
				isFolderAttribute = True
			else
				isFolderAttribute = false
			End If
	End Select
End Function

Function isFileAttribute(strPath,attributes) ' valid attribute hidden, system, readonly, archive, link, and compressed.
	Dim objFSO
	Dim objFile 
	Set objFSO = CreateObject("Scripting.FileSystemObject")
	Set objFile  = objFSO.GetFile(strPath)
	select case lcase(attributes)
		case "hidden"
			If objFile.Attributes AND fAttrHIDDEN Then
				isFileAttribute = True
			else
				isFileAttribute = false
			End If    
		Case "system"
			If objFile.Attributes AND fAttrSYSTEM Then
				isFileAttribute = True
			else
				isFileAttribute = false
			End If   
		Case "readonly"
			If objFile.Attributes AND fAttrREADONLY Then
				isFileAttribute = True
			else
				isFileAttribute = false
			End If
		Case "archive"
			If objFile.Attributes AND fAttrARCHIVE Then
				isFileAttribute = True
			else
				isFileAttribute = false
			End If
		Case "link"
			If objFile.Attributes AND fAttrLINK Then
				isFileAttribute = True
			else
				isFileAttribute = false
			End If
		Case "compressed"
			If objFile.Attributes AND fAttrCOMPRESSED Then
				isFileAttribute = True
			else
				isFileAttribute = false
			End If
	End Select
End Function

Function IsShare(sPath)
	' on error resume next
	CreateObject("Scripting.FileSystemObject").GetFolder(sPath)
	If Err.Number = 0 then
		IsShare = True
	Else
		Err.Clear
		IsShare = False
	End If
	On Error Goto 0
End Function

Function IsUnicodeFile(filename)
	Dim FSO
	Dim ts
	Dim char1
	Dim char2
	Set FSO = createobject("scripting.filesystemobject")
	Set ts = fso.opentextfile(filename)
	IsUnicodeFile = False
	char1 =ts.read(1)
	char2 =ts.read(1)
	ts.close
	if asc(char1) = 255 and asc(char2) = 254 then
		IsUnicodeFile = True
	End If
End Function

Function LargestNumberedSubFolder(sPath)
	' Finds the highest numbered subfolder within
	' a given directory
	Dim FSO
	Dim f
	Dim f1
	Dim fc
	Dim s
	Dim Largest
	Largest = 0
	Set FSO = CreateObject("Scripting.FileSystemObject")
	Set f = FSO.GetFolder(sPath)
	Set fc = f.SubFolders
	'' on error resume next
	For Each f1 in fc
		s = CInt(f1.name)
		If Err.Number = 0 Then
			If s > Largest Then 
				Largest = s
			End If
		Else
			Err.Clear
		End If
	Next
	LargestNumberedSubFolder = Largest
End Function

 Function LinesInFile(FilePath)
	'Returns total line count for sFil
	Dim FSO
	Dim fil
	Set FSO = CreateObject("Scripting.FileSystemObject")
	Set fil = FSO.OpenTextFile(FilePath, FORREADING, SYSTEMDEFAULT)
	fil.Readall
	LinesInFile = fil.Line
End Function

Sub LogItToFile(FilePath, sData)
	' OPT: Removed the FileExists branch — OpenTextFile with FORAPPENDING +
	' CREATEFILE=True will create the file if absent, so the explicit NewFile
	' pre-step and the duplicated With block are both unnecessary.
	If FilePath = "" Then Exit Sub
	With CreateObject("Scripting.FileSystemObject").OpenTextFile(FilePath, FORAPPENDING, True, SYSTEMDEFAULT)
		.Write Now & " | " & sData & vbCrLf
		.Close
	End With
End Sub

Sub LowerCaseFSO(Folspec)
' lowercases all file and folder names within the path specified
	Dim fil
	Dim fils
	Dim fol
	Dim fols
	Dim FSO
	Dim sFolders()
	Dim lngCounter
	Dim sFilName
	Dim sFolName
	ReDim sFolders(0)
	Set FSO = CreateObject("Scripting.FileSystemObject")
	sFolders(0) = FSO.GetAbsolutePathName(Folspec)
	lngCounter = 0
	Do Until lngCounter > UBound(sFolders,1)
	'Next folder to process
		Set fol = FSO.GetFolder(sFolders(lngCounter))
		'Get each file in turn
		Set fils = fol.Files
		If Err.Number <> 0 Then 
			Exit Sub
		end if
		For Each fil In fils
			sFilName = LCase(fil.Name)
			If fil.Name <> sFilName Then
				fil.Name = FSO.GetTempName
				fil.Name = sFilName
			End If
		Next
		'Check for any Sub folders and add them to the folder array
		Set fols = fol.SubFolders
		For Each fol In fols
			If (Lcase(fol.Name) <> "recycled") and (Lcase(fol.Name) <> "$recycle.bin") and (Lcase(fol.Name) <>"system volume information") Then
				ReDim Preserve sFolders(UBound(sFolders,1) + 1)
				sFolders(UBound(sFolders,1)) = fol.Path
			End If
			sFolName = LCase(fol.Name)
			If fol.Name <> sFolName Then
				fol.Name = FSO.GetTempName
				fol.Name = sFolName
			End If
		Next
		lngCounter = lngCounter + 1
	Loop
End Sub

Sub Map(sDrive,UNC,bPersistant)
' Returns boolean for mapping success
	Dim oNet
	' on error resume next
    call LogItToFile(LOGFILE_GLOBAL,"Map Info: | | sDrive=" & sDrive & " UNC=" & UNC & " bPersistant=" & bPersistant)
	if bPersistant =  true then
		Call RunProgramHidden("C:\Windows\System32\Net.exe Use " & sDrive & " " & UNC & "  /PERSISTENT:YES", WAITONRETURN)
	else
		Call RunProgramHidden("C:\Windows\System32\Net.exe Use " & sDrive&" " & UNC & "  /PERSISTENT:NO", WAITONRETURN)
	End If
End Sub

Sub ForceDriveReMap(sDrive,UNC,bPersistant)
' Force a network drive mapping 
	Dim oNet
	'' on error resume next

    call LogItToFile(LOGFILE_GLOBAL,"Map Info: | | sDrive=" & sDrive & " UNC=" & UNC & " bPersistant=" & bPersistant)
	Call RunProgramHidden("C:\Windows\System32\Net.exe Use " & sDrive & " /DELETE", WAITONRETURN)
	if bPersistant =  true then
		Call RunProgramHidden("C:\Windows\System32\Net.exe Use " & sDrive&" " & UNC & "  /PERSISTENT:YES", WAITONRETURN)
	else
		Call RunProgramHidden("C:\Windows\System32\Net.exe Use " & sDrive & " " & UNC & "  /PERSISTENT:NO", WAITONRETURN)
	End If
End Sub

Function ValidDriveLetter(drive)
	Dim ValidLetters
	ValidLetters = "d:e:f:g:h:i:j:k:l:m:n:o:p:q:r:s:t:u:v:w:x:y:z:"
	if instr(1,ValidLetters,ucase(drive),1) = 0 then
		ValidDriveLetter = false
	else
		ValidDriveLetter = true
	end if
End Function

Function TF(x)
	TF = false
	if lcase(x) = "true" Then TF = true
	if lcase(x) = "1" Then TF = true
	if lcase(x) = "yes" Then TF = true
	if lcase(x) = "on" Then TF = true
End Function

Sub MapPrinter(UNCpath,Def)
	Dim netPrinter
	Set netPrinter = CreateObject("WScript.Network")
	netPrinter.AddWindowsPrinterConnection UNCpath
	If (Def = true) then netPrinter.SetDefaultPrinter UNCpath
End Sub

Sub SetDefPrinter(UNCpath)
	Dim netPrinter
	Set netPrinter = CreateObject("WScript.Network")
	' Here is where we set the default printer
	netPrinter.SetDefaultPrinter UNCpath
End Sub

Sub DeletePrinter(UNCpath)
	Dim netPrinter
	Set netPrinter = CreateObject("WScript.Network")
	netPrinter.RemovePrinterConnection UNCpath
	'Deletes printer
End Sub

Sub SaveLocalVar(sVarName, sValue)
	Dim wshShell 
	Dim wshLocalEnv
	if (trim(sVarName) <> "") and (sValue <> "") then
		Set wshShell = CreateObject( "WScript.Shell" )
		Set wshLocalEnv = wshShell.Environment( "USER" )
		wshLocalEnv(sVarName) = trim(sValue)
		Set wshLocalEnv = Nothing
		Set wshShell     = Nothing
	end if
End Sub

function GetLocalEnv(sVarName)
	Dim wshShell 
	Dim wshUserEnv 
	Dim strItem 
	Dim Temp
	Temp = ""
	Set wshShell = CreateObject( "WScript.Shell" )
	Set wshUserEnv = wshShell.Environment( "USER" )
	For Each strItem In wshUserEnv
		Temp = strItem
		if instr(1,Temp,sVarName,1) > 0 then
			exit for
		end if
	Next
	If Temp <> "" then
		Temp = replace(Temp,sVarName&"=","")
	Else 
		Temp = ""
	End If
	Set wshUserEnv = Nothing
	Set wshShell   = Nothing
	GetLocalEnv = Temp
end function

Sub AddPathLocal(NewFold)
	Dim WshUserEnvironment
	Set WshUserEnvironment = WshShell.Environment("User")
	If instr(WshUserEnvironment("PATH"),NewFold & ";") = 0 Then
		If right(WshUserEnvironment("PATH"),1) <> ";" Then
			WshUserEnvironment("PATH") = WshUserEnvironment("PATH") & ";" & NewFold & ";"
		Else
			WshUserEnvironment("PATH") = WshUserEnvironment("PATH") & NewFold & ";"
		End If
	End If
	Set WshUserEnvironment = nothing
End Sub

Function RegExists(sRegKey)
	Dim oShell
    Dim item
	' on error resume next
	Set oShell = CreateObject("WScript.Shell")
	sRegKey = Trim (sRegKey)
	item = oShell.RegRead (sRegKey)
	If Err.number <> 0 Then
		RegExists = False
		Err.clear
	else
		RegExists = true
	End if
End Function

Function GetRegVal(sRegKey)
	Dim oShell
    Dim item
	' on error resume next
	Set oShell = CreateObject("WScript.Shell")
	sRegKey = Trim (sRegKey)
	item = oShell.RegRead( sRegKey)
	if err.number <> 0 then
		call LogItToFile(LOGFILE_GLOBAL,"GetRegVal Error #: " & err.number & " | " & Err.Description & "| sRegKey=" & sRegKey )
		Err.Clear
		GetRegVal = ""
	else
		GetRegVal = item
	End if
End Function

Function IsRunningAsAdministrator() 
    Dim objShell 
    Dim objWMI 
    Dim objProcess 
    Dim colProcesses 
    
    On Error resume next
    
    ' Method: Check via Windows API using Shell command
    Dim oShell 
    Set oShell = CreateObject("WScript.Shell")
    
    ' Try to access a restricted registry key (only admins can write here)
    Dim oReg 
    Set oReg = CreateObject("WScript.Shell")
    
    ' Attempt to read the integrity level via whoami /groups
    Dim oExec 
    Set oExec = oShell.Exec("whoami /groups /fo csv")
    
    Dim sOutput 
    sOutput = oExec.StdOut.ReadAll()
    
    ' Check for "S-1-16-12288" which is the High Mandatory Level SID (Administrator)
    If InStr(sOutput, "S-1-16-12288") > 0 Then
        IsRunningAsAdministrator = True
    Else
        IsRunningAsAdministrator = False
    End If
    
    Exit Function
    
ErrorHandler:
    IsRunningAsAdministrator = False
End Function

Sub UpdateReg(sRegKey,RegValue,RegData,RegType)
     on error resume next
	Dim WSHShell
	Set WSHShell = CreateObject("WScript.Shell")
	if RegData <> "" then
		WSHShell.RegWrite sRegKey,RegValue & "\" & RegData,RegType
	Else
		WSHShell.RegWrite sRegKey,RegValue,RegType
	End if
	if err.number <> 0 then
		call LogItToFile(LOGFILE_GLOBAL,"Update/AddReg Error #: " & err.number & " | " & Err.Description & "| sRegKey=" & sRegKey & " RegValue=" & RegValue & " RegType=" & RegType )
		Err.Clear
	End if	
End Sub

Sub DeleteReg(RegItem)
	' on error resume next
	Dim WSHShell
	Set WSHShell = CreateObject("WScript.Shell")
	WSHShell.RegDelete RegItem
	if err.number <> 0 then
		call LogItToFile(LOGFILE_GLOBAL,"DeleteReg Error #: " & err.number & " | " & Err.Description & " | RegItem=" & RegItem )
		Err.Clear
	End if	
End Sub

Sub md(fldr)
' Recursively create the directory path provided in fldr
' May be used with UNC paths
 ' on error resume next
	Dim aPath
	Dim FSO
	Dim fol
	Dim i
	aPath = Split(fldr,"\")
	Set FSO = CreateObject("Scripting.FileSystemObject")
	fol = aPath(0)
	if err.number <> 0 then
		call LogItToFile(LOGFILE_GLOBAL,"MD Error #: " & err.number & " | " & Err.Description & " | Folder=" & fldr )
		Err.Clear
	End if
	If Len(Replace(fol,":",""))=Len(fol) Then 
	fol = "\\" & fol
	End If
	For i = 1 To UBound(aPath)
		fol = fol & "\" & aPath(i)
		If Not FSO.FolderExists(fol) Then 
			FSO.CreateFolder(fol)
			if err.number <> 0 then
				call LogItToFile(LOGFILE_GLOBAL,"MD Error #: " & err.number & " | " & Err.Description & " | Folder=" & fldr )
				Err.Clear
			End if
		End If
	Next
End Sub

Function MoveFile(oldname,newname)
	Dim fso
	' on error resume next
	Err.Clear
	Set fso = CreateObject("Scripting.FileSystemObject")
	fso.MoveFile oldname,newname
	if err.number <> 0 then
		call LogItToFile(LOGFILE_GLOBAL,"MoveFile Error #: " & err.number & " | " & Err.Description & " | oldname=" & oldname & " newname=" & newname)
		Err.Clear
		MoveFile = False
	Else
		MoveFile = True
	End If
End Function

Function ShowFileAccessInfo(strFileName)
	Dim fso
	Dim f
	Dim s
	Set fso = CreateObject("Scripting.FileSystemObject")
	Set f = fso.GetFile(strFileName)
	s = UCase(strFileName) & VBCRLF
	s = s & "Created: " & f.DateCreated & VBCRLF
	s = s & "Last Accessed: " & f.DateLastAccessed & VBCRLF
	s = s & "Last Modified: " & f.DateLastModified  & VBCRLF
	s = s & "File Size (Bytes): " & f.Size & VBCRLF
	s = s & "File Type: " & f.Type & VBCRLF
	s = s & "Short Name: " & f.ShortName & VBCRLF
	s = s & "Path: "& f.Path & VBCRLF
	s = s & "Attributes: "& f.Attributes & VBCRLF
	ShowFileAccessInfo = s
	Set f = Nothing
	Set fso = Nothing
End Function

Function Tail(fil, nlines)
	' OPT: Original opened the file twice (once to count lines, once to read).
	' Replaced with a single-pass approach using a circular buffer (array).
	Dim buf()
	Dim idx
	Dim count
	Dim line
	Dim i
	ReDim buf(nlines - 1)
	idx = 0
	count = 0
	With CreateObject("Scripting.FileSystemObject").OpenTextFile(fil, FORREADING, False, SYSTEMDEFAULT)
		Do While Not .AtEndOfStream
			buf(idx) = .ReadLine
			idx = (idx + 1) Mod nlines
			If count < nlines Then count = count + 1
		Loop
		.Close
	End With
	' Reconstruct in correct order
	Dim startIdx
	startIdx = (idx - count + nlines) Mod nlines
	Tail = ""
	For i = 0 To count - 1
		If i > 0 Then Tail = Tail & vbCrLf
		Tail = Tail & buf((startIdx + i) Mod nlines)
	Next
End Function

Function TempFileName
	TempFileName = CreateObject("Scripting.FileSystemObject").GetTempName
End Function

Function TempFileBase
	Dim FolTemp
	Dim i
	Dim chrList
	Dim uLimit
	Dim sTmp
	Dim FSO
	Set FSO = CreateObject("Scripting.FileSystemObject")
	Set folTemp = FSO.GetSpecialFolder(TEMPFOLDER)
	 ' Returns path to a unique, randomly generated file name (.tmp)
	 ' within the %TEMP% folder
	 'Following sequence allows 360,040,606,269,696 unique file names
	chrList = "abcdefghijklmnopqrstuvwxyz0123456789,~-_&[]()#@!`;+="
	uLimit = Len(chrList)
	Randomize
	For i = 1 To 8
		sTmp = sTmp & Mid(chrList, ((uLimit) * Rnd + 1), 1)
	Next
	TempFileBase = folTemp & "\" & sTmp & ".tmp"
End Function

Sub TrimFileSize(strFile,toSize)
' Based on (lightly) modified code from Eric Phelps
' This script reduces the size of line-oriented log files.
' It erases the first (oldest) lines in a log file until
' it gets the log down below the specified size.
	Dim ts
	Dim fs
	Dim sText
	Set fs = CreateObject("Scripting.FileSystemObject")
	' If it fails anywhere, just quit.
	' on error resume next
	' Read the file
	Set ts = fs.OpenTextFile(strFile, FORREADING)
	If Err.Number <> 0 Then 
		exit Sub
	End If
	sText = ts.ReadAll
	ts.Close
	' See if the file is already below desired size
	If Len(sText) < toSize Then
		exit Sub 
	End If 
	sText = trim(Right(sText, toSize))  ' Trim the file
	If (Instr(sText, vbCrlf) = 0) Or (Instr(sText, vbCrlf) + 3 > Len(sText)) Then
		exit sub
	Else
		sText = Mid(sText, Instr(sText, vbCrlf) + 2)
	End If
	 ''''''''''Write the shortened file
	Set ts = fs.OpenTextFile(strFile, FORWRITING)
	If Err.Number = 0 Then 
		ts.Write sText
		ts.Close
	End IF
End Sub

Function VerFile(sPath)
	' on error resume next
	VerFile = 0
	If IsFile(sPath) Then
		VerFile = CreateObject("Scripting.FileSystemObject").GetFileVersion(sPath)
	End If
	Err.Clear
End Function

Sub DeleteFile(strFileName)
  Dim objFS
  Set objFS = CreateObject("Scripting.FileSystemObject")
  If objFS.FileExists(strFileName) Then
    objFS.DeleteFile strFileName, true
  End If
  Set objFS = nothing
End Sub

Sub WriteFile(strFileName, strContent, fileType)  'fileType = ASCII or UNICODEFILE
  Dim objFS
  Dim objFSFile
  Set objFS = CreateObject("Scripting.FileSystemObject")
  Set objFSFile = objFS.CreateTextFile(strFileName, CREATEFILE, fileType)
  objFSFile.Write(strContent)
  objFSFile.Close
  Set objFSFile = nothing
  Set objFS = nothing
End Sub

Sub AppendFile(strFileName, strContent,fileType) 'fileType = ASCII or UNICODEFILE
  Dim objFS
  Dim objTS
  Set objFS = CreateObject("Scripting.FileSystemObject")
  Set objTS = objFS.OpenTextFile(strFileName, FORAPPENDING , CREATEFILE, fileType )
  objTS.writeLine(strContent)
  objTS.close()
  Set objTS = nothing
  Set objFS = nothing
End Sub

Function ReadFile(strFileName,fileType) 'fileType = ASCII or UNICODEFILE or SYSTEMDEFAULT
  Dim objFS
  Dim objTS
  Dim strContents
  strContents = ""
  Set objFS = CreateObject("Scripting.FileSystemObject")
  If objFS.FileExists(strFilename) Then
    Set objTS = objFS.OpenTextFile(strFileName, FORREADING , DONOTCREATEFILE , fileType )
    strContents = objTS.ReadAll
    objTS.Close
    Set objTS = nothing
  End If
  Set objFS = nothing
  Readfile = strContents
  End Function

Sub RunProgram(CMDLine,Wait)
	Dim objShell
	Dim ReturnCode
	'if CMDLine="" or isnull(CMDLine) then exit sub
	Set objShell = WScript.CreateObject ("WScript.shell")
	if trim(CMDLine) <> "" then ReturnCode = objShell.run(CMDLine, DISPLAY_WINDOW, Wait)
	if err.number <> 0 then
		Err.Clear
	End if
	call LogItToFile(LOGFILE_GLOBAL,"RunProgram ReturnCode: " & ReturnCode & " | " & CMDLine & "| ReturnCode=" & ReturnCode & " CMDLine=" & CMDLine & " Wait=" & Wait)
	Set objShell = Nothing
End Sub

Sub RunProgramHidden(CMDLine,Wait)
	Dim objShell
	Dim ReturnCode
	'if CMDLine="" or isnull(CMDLine) then exit sub
	Set objShell = WScript.CreateObject ("WScript.shell")
	if trim(CMDLine) <> "" then ReturnCode = objShell.run(CMDLine, HIDE_WINDOW, Wait)
	if err.number <> 0 then
		Err.Clear
	End if
	call LogItToFile(LOGFILE_GLOBAL,"RunProgramHidden ReturnCode: " & ReturnCode & " | " & CMDLine & "| ReturnCode=" & ReturnCode & " CMDLine=" & CMDLine & " Wait=" & Wait)
	Set objShell = Nothing
End Sub

Sub KillSession()
	Call RunProgramHidden("%SystemRoot%\System32\Shutdown.exe /l /f /t 0",NOWAIT)
End Sub

Sub KillProcess(Process)
	' on error resume next
	Dim objWMIService
	Dim colProcessList
	Dim objProcess
	Set objWMIService = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\root\cimv2")
	Set colProcessList = objWMIService.ExecQuery("Select * from Win32_Process Where Name = '" & Process & "'")
	For Each objProcess in colProcessList
		objProcess.Terminate()
	Next
	Set objWMIService = nothing
End Sub

Function FindOSVersionMajorMinor
	Dim objWMI
	Dim objItem
	Dim colItems
	Dim OSVersion
	Dim Temp
	Set objWMI = GetObject("winmgmts:\\.\root\cimv2")
	Set colItems = objWMI.ExecQuery("Select * from Win32_OperatingSystem",,48)
	For Each objItem in colItems
		OSVersion = objItem.Version
	Next
	Temp = Split(OSVersion,".")
	FindOSVersionMajorMinor = Temp(0) & "." & temp(1)
	Set colItems = Nothing
	Set objWMI = Nothing
End Function 

Function FindOSVersionFull
	Dim objWMI
	Dim objItem
	Dim colItems
	Dim OSVersion
	Set objWMI = GetObject("winmgmts:\\.\root\cimv2")
	Set colItems = objWMI.ExecQuery("Select * from Win32_OperatingSystem",,48)
	For Each objItem in colItems
		OSVersion = objItem.Version
	Next
	FindOSVersionFull = OSVersion
	Set colItems = Nothing
	Set objWMI = Nothing
End Function 

Sub XCopy(strFrom, strTo)
	Dim strcommand
	Dim i
	Dim Shell
	Set Shell = WScript.CreateObject("WScript.SHELL")
	strcommand = SHELL.ExpandEnvironmentStrings("%systemroot%") & "\System32\xcopy.exe " & chr(34) & strFrom &  chr(34) & " " & strTo & "\"  & " /S /Y "
	i = Shell.Run(strcommand, HIDE_WINDOW, WAITONRETURN)
	call LogItToFile(LOGFILE_GLOBAL,"XCopy: Result=" & i & " |  | strFrom=" & strFrom & " strTo=" & strTo)
End Sub

Sub CopyFolder(sFol,dFol)
	' on error resume next
	Dim c
	Dim fso
	Dim dest
	Dim fName
	'c = Len(sFol) - Len(Replace(sFol, "\", "", 1))
	fName = Basename(sFol)  ', InStr(1, Application.Substitute(sFol, "\", "*", c), "*") + 1)
	if right(dFol,1) <> "\" then
		dest = dFol & "\" & fName
	else
		dest = dFol & fName
	End if
	Set fso = CreateObject("Scripting.FileSystemObject")
	If Not fso.FolderExists(dest) Then
			fso.CopyFolder sFol, dFol
			if err.number <> 0 then
				call LogItToFile(LOGFILE_GLOBAL,"CopyFolder Error #: " & err.number & " | " & Err.Description & " | sFol=" & sFol & " dFol=" & dFol)
				Err.Clear
			End if	
	Else
		call LogItToFile(LOGFILE_GLOBAL,"CopyFolder Warning #: Folder Already Exist: | | sFol=" & sFol & " dFol=" & dFol)
	End If
End Sub

Sub Copy(strFrom, strTo, OverWrite)
	' on error resume next
	Dim objFSO
	Set objFSO = CreateObject("Scripting.FileSystemObject")
	objFSO.CopyFile strFrom , strTo , OverWrite
	if err.number <> 0 then
		call LogItToFile(LOGFILE_GLOBAL,"Copy Error #: " & err.number & " | " & Err.Description & " | strFrom=" & strFrom & " strTo=" & strTo & " OverWrite=" & OverWrite)
		Err.Clear
	End if
End Sub

Sub NTFSCompressFolder(strFolder)
	Dim strcommand
	Dim i
	Dim Shell
	Set Shell = WScript.CreateObject("WScript.SHELL")
	strcommand = SHELL.ExpandEnvironmentStrings("%systemroot%") & "\System32\COMPACT.exe /C " & chr(34) & strFolder &  chr(34) & " "
	i = Shell.Run(strcommand, HIDE_WINDOW, WAITONRETURN)
	call LogItToFile(LOGFILE_GLOBAL,"NTFSCompressFolder: Result=" & i & " |  | Folder=" & strFolder)
End Sub  

Function NTFSCompressFile(strfile)
	dim objWMI
	dim objFile
	dim strErr
	Set objWMI = GetObject("winmgmts:\\.\root\cimv2")
	Set objFile = objWMI.Get("Cim_Datafile='" & strFile & "'")
	strErr = objFile.Compress
	If strErr <> 0 Then
		NTFSCompress = strErr
	Else
		NTFSCompress = 0
	End If
End Function

Sub QuitNow(x)
	WScript.quit(x)
End Sub

Function ExportReg(strReg, strWhere)
	Dim strcommand
	Dim Shell
	Dim i
	Set Shell = WScript.CreateObject("WScript.SHELL")
	strcommand = Shell.ExpandEnvironmentStrings("%systemroot%") & "\System32\REG export " & chr(34) & strReg & chr(34) & " " & chr(34) & strWhere & chr(34) & " /y /reg:32"
	i = Shell.Run(strcommand, HIDE_WINDOW, WAITONRETURN)
	ExportReg = i
	call LogItToFile(LOGFILE_GLOBAL,"ExportReg: Result=" & i & " |  | strReg=" & strReg & " strWhere=" & strWhere)
End Function

Function ImportReg(strRegFile)
	Dim strcommand
	Dim Shell
	Dim i
	Set Shell = WScript.CreateObject("WScript.SHELL")
	strcommand = Shell.ExpandEnvironmentStrings("%systemroot%") & "\System32\REG import " & chr(34) & strRegFile & chr(34) & " /reg:32"
	i = Shell.Run(strcommand, HIDE_WINDOW, WAITONRETURN)
	ImportReg = i
	call LogItToFile(LOGFILE_GLOBAL,"ImportReg: Result=" & i & " |  | strRegFile=" & strRegFile )
End Function

Sub RemoveFoldersOverNDays(strFolder,NDays)
	Dim strcommand1
	Dim strcommand2
	Dim Fol
	Dim i
	Dim Shell
	Set Shell = WScript.CreateObject("WScript.SHELL")
	if right(trim(strFolder),1) = "\" then
		fol = left(strFolder,len(strFolder)-1)
	else
		fol = strFolder
	end if
	strcommand1 = SHELL.ExpandEnvironmentStrings("%systemroot%") & "\System32\forfiles.exe /p """ & fol & """ /d -" & NDays & " /c """ & "CMD /Q /C @rmdir /S /Q @PATH""" 
	strcommand2 = SHELL.ExpandEnvironmentStrings("%systemroot%") & "\System32\forfiles.exe /p """ & fol & """ /d -" & NDays & " /c """ & "CMD /Q /C del /F /Q @PATH"""
	i = Shell.Run(strcommand1, HIDE_WINDOW, WAITONRETURN)
	i = Shell.Run(strcommand2, HIDE_WINDOW, WAITONRETURN)
End Sub

Function GetEnv(VVar)
	Dim Shell
	Dim VV
	' on error resume next
	'if instr(1,VVar,"%",1) = 0 then VV = "%" & trim(VVar) & "%"
	If InStr(1, VVar, "%", 1) = 0 Then
		VV = "%" & Trim(VVar) & "%"
	Else
		VV = VVar
	End If	
	
	Set Shell = WScript.CreateObject("WScript.SHELL")
	GetEnv = Shell.ExpandEnvironmentStrings(VV)
	if err.number <> 0 then
		Call LogItToFile(LOGFILE_GLOBAL,"GetEnv Error #: " & err.number & " | " & Err.Description & vbcrlf)	
		err.clear
	end if
End Function

Function ExecProgram(strProgram)
	Dim objShell
	Dim objWshScriptExec
	Set objShell = CreateObject("WScript.Shell")
	Set objWshScriptExec = objShell.Exec(strProgram)
	ExecProgram = objWshScriptExec.ProcessID
End function

Function ExecProgramCaptureOutput(strProgram)
	Dim objShell
	Dim objWshScriptExec
	Dim objStdOut	
	Dim strOutput
	Set objShell = CreateObject("WScript.Shell")
	Set objWshScriptExec = objShell.Exec(strProgram)
	Set objStdOut = objWshScriptExec.StdOut
	strOutput = objStdOut.ReadAll
	ExecProgramCaptureOutput = strOutput & "EXITCODE=" & objWshScriptExec.ExitCode
End function

Function ExecProgramCaptureDelimitedOutput(strProgram,delimiter,OutputType)
	Dim objShell
	Dim objWshScriptExec
	Dim objOutput
	'Dim strOutput
	Dim strLine
	Dim Temp
	strLine = ""
	Set objShell = CreateObject("WScript.Shell")
	Set objWshScriptExec = objShell.Exec(strProgram)
	if OutputType = STDERR then
		Set objOutput = objWshScriptExec.StdErr
	else
		Set objOutput = objWshScriptExec.StdOut
	end if
	While Not objOutput.AtEndOfStream
		Temp = trim(objOutput.ReadLine)
		if Temp <> "" then strLine = strLine & Temp & delimiter
	Wend
	ExecProgramCaptureDelimitedOutput = strLine & "EXITCODE=" & objWshScriptExec.ExitCode
End function

Function GetADGroups(Domain, UserName)
	' on error resume next
	Dim DSo
	Dim adsPath
	Dim objADUser
	Dim ADGroup
	Dim ADGroupList
	Set DSo = GetObject("WinNT:")
	adsPath = "WinNT://" & Domain & "/" & UserName	
	Set objADUser = DSo.OpenDSObject(adsPath,"","", ADS_READONLY_SERVER)
	For each ADGroup in objADUser.groups
		ADGroupList = ADGroupList & ADGroup.name & "|"
	Next
	GetADGroups = ADGroupList
End Function

Function VerifyRunningProcesses(ApplicationName,AppCaption,OtherwiseMsg,StopProcsList)
	Dim Procs
	Dim colProcesses
	Dim i
	Dim x
	Dim StillRunning
	Dim WshShell
	Dim objWMIService
	Dim btn
	Dim VP
	Procs = Split(StopProcsList,",")
	x = 1
	i = 0
	Set WshShell = WScript.CreateObject("WScript.Shell")
	Set objWMIService = GetObject("winmgmts:{impersonationLevel=impersonate}!\\.\root\cimv2")
	Do Until (x = 0)  
		x = 0 
		StillRunning = "The following applications are still running:" & vbCrLf & vbcrlf
		For i = 0 To UBound(Procs)
			Set colProcesses = objWMIService.ExecQuery("Select * from Win32_Process Where Name = '" & Left(Procs(i),InStr(1,Procs(i),"|",1)-1) & "'")
			If colProcesses.Count = 0 Then	' is not running
			Else
				x = x + 1	' is running."
				StillRunning = StillRunning & "     " & Replace(mid(Procs(i),InStr(1,Procs(i),"|",1)+1,Len(Procs(i))),",","") & vbcrlf
			End If
		Next
		If x > 0 Then 
			VP = WshShell.Popup(StillRunning & vbCrLf & vbCrLf & "To continue the " & ApplicationName & " installation, please" & vbcrlf & "close the above listed applications And click OK." & vbCrLf & vbCrLf & OtherwiseMsg & vbCrLf, NO_TIMEOUT, AppCaption, QUESTION + OK_CANCEL )
			If VP = CANCEL_CLICKED Then 
				x = 0
			End If
		End If
	Loop
	VerifyRunningProcesses = VP
End Function

Function Resolve(Item)
	Dim VariableFound
	Dim i
	i = instr(1,Item,"~",1)
	if (i > 0) and (len(Item) >= 3) then VariableFound = Mid(Item,i,3)
	Select Case ucase(VariableFound)
		Case "~UP" : Resolve = replace(Item,"~UP",USERPROFILE_GLOBAL)
		Case "~UN" : Resolve = replace(Item,"~UN",CURRENTUSER_GLOBAL)
		Case "~UT" : Resolve = replace(Item,"~UT",TEMPFOLDER_GLOBAL)
		Case "~CN" : Resolve = replace(Item,"~CN",COMPUTERNAME_GLOBAL)
		Case "~AD" : Resolve = replace(Item,"~AD",APPDATA_GLOBAL)
		Case "~CS" : Resolve = replace(Item,"~CS",COMSPEC_GLOBAL)
		Case "~DN" : Resolve = replace(Item,"~DN",DOMAINNAME_GLOBAL)
		Case "~HS" : Resolve = replace(Item,"~HS",HOMEUNC_GLOBAL)
		Case "~PD" : Resolve = replace(Item,"~PD",PROGRAMDATA_GLOBAL)
		Case "~P6" : Resolve = replace(Item,"~P6",PROGRAMFILES_GLOBAL)
		Case "~P3" : Resolve = replace(Item,"~P3",PROGRAMW6432_GLOBAL)
		Case "~PU" : Resolve = replace(Item,"~PU",PUBLICFOLDER_GLOBAL)
		Case "~SY" : Resolve = replace(Item,"~SY",SYSTEMDRIVE_GLOBAL)
		Case "~AU" : Resolve = replace(Item,"~AU",ALLUSERSPROFILE_GLOBAL)
		Case "~LA" : Resolve = replace(Item,"~LA",LOCALAPPDATA_GLOBAL)
		Case "~HL" : Resolve = replace(Item,"~HL",HOMEDRIVELETTER_GLOBAL)
		Case "~HP" : Resolve = replace(Item,"~HP",HOMEPATH_GLOBAL)
		Case "~CD" : Resolve = replace(Item,"~CD",CURRENTWORKINGFOLDER_GLOBAL)
		Case "~UD" : Resolve = replace(Item,"~UD",USERDISPLAYNAME_GLOBAL)
		Case Else  : Resolve = Item
	End Select
End Function

Function GetVal(x)
	select case ucase(trim(x))
		case "" 	: GetVal = 0
		case "yes" 	: GetVal = 1000
		case "1" 	: GetVal = 1000
		case "on" 	: GetVal = 1000
		case "0" 	: GetVal = 0
		case "no" 	: GetVal = 0
		case "off" 	: GetVal = 0
		Case Else 	: GetVal = cint(x)
	End Select
End Function

Sub Delay(x)
	if x <> 0 then wscript.sleep x
End Sub

Function Evalf(strWhat,strItem,Op,val)
	' OPT: Collapsed duplicate "eq"/"equ" and "ne"/"neq" cases.
	' Also replaced repetitive If/Else True/False patterns with direct assignment.
	Dim rval 
	Dim funct
	Dim Condition
	Set funct = getref(strWhat)
	If lcase(val) = "true" Then
		rval = True
	ElseIf lcase(val) = "false" Then 
		rval = False
	Else
		rval = val
	End If 
	Condition = funct(strItem) 
	Set funct = Nothing 
	Select Case lcase(Op)
		Case "eq",  "equ" : Evalf = (Condition =  rval)
		Case "neq", "ne"  : Evalf = (Condition <> rval)
		Case "gt"         : Evalf = (Condition >  rval)
		Case "lt"         : Evalf = (Condition <  rval)
		Case "geq"        : Evalf = (Condition >= rval)
		Case "leq"        : Evalf = (Condition <= rval)
		Case "in"         : Evalf = (instr(1,Condition,rval,1) > 0)
		Case "notin"      : Evalf = (instr(1,Condition,rval,1) = 0)
	End Select
End Function

' OPT: New helper — replaces the repeated inline guard used in every
' LoginXxx sub.  Centralising it means one edit fixes all callers.
Function EnvMatch(WTS, PC)
	EnvMatch = ((WTSENV = True) And (TF(WTS) = True)) Or _
	            ((PCENV  = True) And (TF(PC)  = True))
End Function

Sub LoginDoClearProcess(ProcList,Caption,ElseMsg,ForProcName,WTS,PC,QCode)
	Dim Result
	Result = 0
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		Result = VerifyRunningProcesses(ForProcName,Caption,ElseMsg,ProcList)
		If Result = CANCEL_CLICKED then 
			call LogItToFile(LOGFILE_GLOBAL,"User Cancelled: " & QCode & " | Cancelled | ")
			Call QuitNow(QCode)
		End if
	End If
End Sub

Sub LoginDoShowProgress(ProgBarMsg,strBanner,TType,WTS,PC)
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		Call RunProgram("%SystemRoot%\System32\mshta.exe " & CURRENTWORKINGFOLDER_GLOBAL & "\ShowProgress" & TType & ".hta cmd=" & chr(34) & Resolve(ProgBarMsg) & chr(34) & " ~ " & chr(34) & Resolve(strBanner) & chr(34) & " ",NOWAIT)
	End If
End Sub

Sub LoginDoMessage(Msg, Caption, TimeOut, WTS, PC)
	Dim WshShell
	Set WshShell = WScript.CreateObject("WScript.Shell")
	If TimeOut = ""  then TimeOut = 0
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		WshShell.Popup Resolve(Msg) , TimeOut, Resolve(Caption), 64		
	End If
End Sub

Sub LoginDoCopy(File,Dest,OverWrite,WTS,PC)
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		Call Copy(Resolve(File),Resolve(Dest),TF(OverWrite))
	End If
End Sub

Sub LoginDoCopyFolder(Folder,Dest,WTS,PC)
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		Call CopyFolder(Resolve(Folder),Resolve(Dest))
	End If
End Sub

'LoginMapDevice(Drive,UNC,ForceMap,Persistent,WTS,PC)
Sub LoginMapDevice(Drive,UNC,ForceMap,Persistent,WTS,PC,Def,DelayBefore,DelayAfter)
	Dim DR
	Dim UN
	UN = Resolve(UNC)
	DR = Resolve(Drive)
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		If ValidDriveLetter(DR) = true then
			Call Delay(GetVal(DelayBefore))
			If TF(ForceMap) = false then 
				Call Map(DR,UN,TF(Persistent))
			else
				Call ForceDriveReMap(DR,UN,TF(Persistent))
			end if
			Call Delay(GetVal(DelayAfter))
		End if
		If instr(1,DR,"\\",1) > 0 then
			Call MapPrinter(DR,TF(Def))
		End if
	End If
End Sub

Sub LoginRunProgram(CMD,Arg,Wait,Visible,WTS,PC,Bits)
	Dim CMDLine
	CMDLine = ""
	if (instr(1,CMD," ",1) > 0) then 
		CMDLine = chr(34) & CMD & chr(34) & " " & trim(Arg)
	Else
		CMDLine = CMD & " " & trim(Arg)
	End If
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		if ((instr(1,Bits,"64",1) > 0) and (Is64BitOS = true)) or ((instr(1,Bits,"32",1) > 0) and (Is32BitOS = true)) then 
			if (TF(Visible) = true) then 
				Call RunProgram(CMDLine,TF(Wait))
			Else
				Call RunProgramHidden(CMDLine,TF(Wait))
			End If
		end if
	End IF
End Sub

Sub LoginAddPath(sValue, WTS, PC)
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		Call AddPathLocal(sValue)
	end if
End Sub

Sub LoginUpdateEVar(sVarName, sValue, WTS, PC)
	'' on error resume next
	Dim VariableValue
	'Dim objWMIService
	'Dim colItems
	'Dim objItem
	Dim appendVar
	appendVar = 0
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then	
		If instr(sVarName,"%") <> 0 Then
			If Left(sVarName,1) = "%" Then 
				appendVar = 1  'Append left
			Else
				appendVar = 2	'Append right
			End If
			sVarName = replace(sVarName,"%","")
		End If
		VariableValue = GetLocalEnv(sVarName)
		If Right(sValue,1) <> ";" Then
			sValue = sValue & ";"
		End If
		If (VariableValue <> "") Then
			If appendVar > 0 Then
				If Right(VariableValue,1) <> ";" Then
					VariableValue = VariableValue & ";"
				End If
				If InStr(VariableValue,sValue) = 0 Then
					If appendVar = 1 Then
						SaveLocalVar sVarName, sValue & VariableValue
					Else
						SaveLocalVar sVarName, VariableValue & sValue
					End If
				End If
			Else
				SaveLocalVar sVarName, sValue
			End If
		Else 
			SaveLocalVar sVarName, sValue
		End If
	End If 
End Sub

Sub LoginUpdateReg(RegKey,RegValue,RegData,RegType,PC,WTS,OP)
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then	
		Select Case lcase(OP)
			' OPT: "update" and "add" were identical — merged into one Case.
			Case "update", "add" : Call UpdateReg(RegKey,Resolve(RegValue),Resolve(RegData),RegType)
			Case "delete"        : Call DeleteReg(Resolve(RegKey))
		End Select
	End If
End Sub

Sub LoginRunIFFunction(strWhat,strItem,Op,vval,strDo,DoArg,strElseDo,ElseDoArg,strWait,Visible,WTS,PC,Bits)
	Dim Condition 
	Dim CMDLine
	Dim ElseCMDLine
	CMDLine = ""
	ElseCMDLine = ""
	if (instr(1,strDo," ",1) > 0) then 
		CMDLine = chr(34) & strDo & chr(34) & " " & trim(DoArg)
	Else
		CMDLine = strDo & " " & trim(DoArg)
	End If	
	if (instr(1,strElseDo," ",1) > 0) then 
		ElseCMDLine = chr(34) & strElseDo & chr(34) & " " & trim(ElseDoArg)
	Else
		ElseCMDLine = strElseDo & " " & trim(ElseDoArg)
	End If
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		if ((instr(1,Bits,"64",1) > 0) and (Is64BitOS = true)) or ((instr(1,Bits,"32",1) > 0) and (Is32BitOS = true)) then 
			Condition = Evalf(strWhat,Resolve(strItem),Op,Resolve(vval))
			If Condition = true Then
				if TF(Visible) = true then 
					Call RunProgram(CMDLine,TF(strWait))
				else
					Call RunProgramHidden(CMDLine,TF(strWait))
				end if
			Else
				If strElseDo <> "" Then
					if TF(Visible) = true then 
						Call RunProgram(ElseCMDLine,TF(strWait))
					else
						Call RunProgramHidden(ElseCMDLine,TF(strWait))
					end if			
				End If 
			End If 
		End If
		If Err.Number <> 0 Then
			Err.clear
		End If
	End If 
End Sub 

Sub CreateFunction(FolderFileShortcut,TType,OverWrite,WTS,PC,Cmdline,WorkDir,Icon,Location,HotKey,WindowStyle)
	Dim x
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		If lcase(TType) = "folder" then
			Call MD(Resolve(FolderFileShortcut))
		Elseif lcase(TType) = "file" then
			Call NewFile(Resolve(FolderFileShortcut),TF(OverWrite))
		Elseif lcase(TType) = "shortcut" then 
			Call CreateShortCut(Resolve(Cmdline),Resolve(WorkDir),Resolve(Icon),Resolve(FolderFileShortcut),Resolve(Location),HotKey,WindowStyle)
		End If
	End If
End Sub

Sub LoginDoEndProgress(WTS,PC)
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		call RunProgramHidden("%SystemRoot%\System32\TaskKill.exe /IM mshta.exe /F",true)
	End If
End Sub

Sub LoginRunIFVarFunction(VVar,Op,vval,strDo,DoArg,strElseDo,ElseDoArg,strWait,Visible,WTS,PC, Bits)
	Dim Condition 
	Dim CMDLine
	Dim ElseCMDLine
	CMDLine = ""
	ElseCMDLine = ""
	if (instr(1,strDo," ",1) > 0) then 
		CMDLine = chr(34) & strDo & chr(34) & " " & trim(DoArg)
	Else
		CMDLine = strDo & " " & trim(DoArg)
	End If	
	if (instr(1,strElseDo," ",1) > 0) then 
		ElseCMDLine = chr(34) & strElseDo & chr(34) & " " & trim(ElseDoArg)
	Else
		ElseCMDLine = strElseDo & " " & trim(ElseDoArg)
	End If	
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		if ((instr(1,Bits,"64",1) > 0) and (Is64BitOS = true)) or ((instr(1,Bits,"32",1) > 0) and (Is32BitOS = true)) then 
			Condition = Evalf("GetEnv",VVar,Op,Resolve(vval))
			If Condition = true Then
				if TF(Visible) = true then 
					Call RunProgram(CMDLine,TF(strWait))
				else
					Call RunProgramHidden(CMDLine,TF(strWait))
				end if
			Else
				If strElseDo <> "" Then
					if TF(Visible) = true then 
						Call RunProgram(ElseCMDLine,TF(strWait))
					else
						Call RunProgramHidden(ElseCMDLine,TF(strWait))
					end if			
				End If 
			End If 
		End If
		If Err.Number <> 0 Then
			Err.clear
		End If
	End If 
End Sub 

Sub LoginSetRegIfVarFunction(VVar,Op,Val,RegKey,RegValue,RegType,WTS,PC)	
	Dim Condition 
	If ((WTSENV = true) and (TF(WTS) = true)) or ((PCENV = true) and (TF(PC) = true)) Then
		Condition = Evalf("GetEnv",VVar,Op,Val)
		If Condition = true Then
			Call UpdateReg(RegKey,Resolve(RegValue),"",RegType)
		End If
	End If
End Sub

Sub ParseAndRunProgram(elem, tagName)
    Dim Parameters, SubItem, Tmp1, Tmp2, Tmp3, Tmp4, Tmp5, Tmp6, Tmp7
    For Each Parameters In elem.selectNodes(tagName)
        For Each SubItem In Parameters.Attributes
            Select Case SubItem.nodeName
                Case "CMD"     : Tmp1 = SubItem.Text
                Case "Arg"     : Tmp2 = SubItem.Text
                Case "Wait"    : Tmp3 = SubItem.Text
                Case "Visible" : Tmp4 = SubItem.Text
                Case "WTS"     : Tmp5 = SubItem.Text
                Case "PC"      : Tmp6 = SubItem.Text
                Case "Bits"    : Tmp7 = SubItem.Text
            End Select
        Next
        Call LoginRunProgram(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5,Tmp6,Tmp7)
    Next
End Sub

Sub DoSteps(xmlDoc,Context,AllADGroupsList,StrUser)
	Dim globalList
	Dim ElemList
	Dim elem
	Dim itm	
	Dim SubItem
	Dim Parameters
	Dim Tmp1
	Dim Tmp2
	Dim Tmp3
	Dim Tmp4
	Dim Tmp5
	Dim Tmp6
	Dim Tmp7
	Dim Tmp8
	Dim Tmp9
	Dim Tmp10
	Dim Tmp11
	Dim Tmp12
	Dim Tmp13
	Dim Shortname
	Dim GroupComputerOrUserName
	Dim i
	Dim ComputerMatch
	i = 0
	Shortname = ""
	Set globalList = xmlDoc.getElementsByTagName(Context)
	For Each elem In globalList
		For Each itm In elem.Attributes	
			Select Case itm.nodeName
				Case "Name" : GroupComputerOrUserName = lcase(itm.Text)
			End Select
		Next
		If Context = "Computer" Then
			ComputerMatch = false
			i = instr(1,GroupComputerOrUserName,"*",1)
			if i > 0 then 
				Shortname = Left(GroupComputerOrUserName,i-1)
				if (lcase(Shortname) = (lcase(Left(COMPUTERNAME_GLOBAL,i-1)))) then 
					ComputerMatch = true
				End if
			End If
			If lcase(COMPUTERNAME_GLOBAL) = lcase(GroupComputerOrUserName) then
				ComputerMatch = true
			End If
		End If
		if (instr(1,lcase(AllADGroupsList),GroupComputerOrUserName,1)>0) or (ComputerMatch = true) or (lcase(StrUser) = GroupComputerOrUserName) or (Context = "Global") or (instr(1,Context,"TaskGroup",1) > 0) then
			For Each Parameters In elem.selectNodes("ClearProcess")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "ProcList" 	: Tmp1 = SubItem.Text
						Case "Caption" 		: Tmp2 = SubItem.Text
						Case "ElseMsg" 		: Tmp3 = SubItem.Text
						Case "ForProcName" 	: Tmp4 = SubItem.Text
						Case "WTS" 			: Tmp5 = SubItem.Text
						Case "PC" 			: Tmp6 = SubItem.Text
					End Select
				Next
				Call LoginDoClearProcess(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5,Tmp6,777)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
				Tmp6 = ""				
			Next
			For Each Parameters In elem.selectNodes("ShowProgress")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "BarText" 		: Tmp1 = SubItem.Text
						Case "Text" 		: Tmp1 = SubItem.Text
						Case "Banner" 		: Tmp2 = SubItem.Text
						Case "Type"			: Tmp3 = SubItem.Text
						Case "WTS" 			: Tmp4 = SubItem.Text
						Case "PC" 			: Tmp5 = SubItem.Text
					End Select
				Next
				Call LoginDoShowProgress(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
			Next
			For Each Parameters In elem.selectNodes("PopupMessageFirst")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "Text" 		: Tmp1 = SubItem.Text
						Case "Caption" 		: Tmp2 = SubItem.Text
						Case "TimeOut" 		: Tmp3 = SubItem.Text
						Case "WTS" 			: Tmp4 = SubItem.Text
						Case "PC" 			: Tmp5 = SubItem.Text
					End Select
				Next
				Call LoginDoMessage(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
			Next
			For Each Parameters In elem.selectNodes("SetEnv")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "Var" 		: Tmp1 = SubItem.Text
						Case "Value" 	: Tmp2 = SubItem.Text
						Case "WTS" 		: Tmp3 = SubItem.Text
						Case "PC" 		: Tmp4 = SubItem.Text
					End Select
				Next
				Call LoginUpdateEVar(Tmp1,Tmp2,Tmp3,Tmp4)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
			Next
			For Each Parameters In elem.selectNodes("AddPath")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "Path" 	: Tmp1 = SubItem.Text
						Case "WTS" 		: Tmp2 = SubItem.Text
						Case "PC" 		: Tmp3 = SubItem.Text
					End Select
				Next
				Call LoginAddPath(Tmp1,Tmp2,Tmp3)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
			Next			
			For Each Parameters In elem.selectNodes("Reg")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "Key" 		: Tmp1 = SubItem.Text
						Case "Value" 	: Tmp2 = SubItem.Text
						Case "Data"		: Tmp3 = SubItem.Text
						Case "Type" 	: Tmp4 = SubItem.Text
						Case "PC" 		: Tmp5 = SubItem.Text
						Case "WTS" 		: Tmp6 = SubItem.Text
						Case "Op"		: Tmp7 = SubItem.Text
					End Select
				Next
				Call LoginUpdateReg(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5,Tmp6,Tmp7)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
				Tmp6 = ""
				Tmp7 = ""
			Next
			For Each Parameters In elem.selectNodes("Map")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "Drive" 		: Tmp1 = SubItem.Text
						Case "Printer" 		: Tmp1 = SubItem.Text
						Case "UNC" 			: Tmp2 = SubItem.Text
						Case "ForceMap"		: Tmp3 = SubItem.Text
						Case "Persistent"	: Tmp4 = SubItem.Text
						Case "WTS" 			: Tmp5 = SubItem.Text
						Case "PC" 			: Tmp6 = SubItem.Text
						Case "Default" 		: Tmp7 = SubItem.Text
						Case "DelayBefore"  : Tmp8 = SubItem.Text
						Case "DelayAfter"   : Tmp9 = SubItem.Text
					End Select
				Next
				Call LoginMapDevice(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5,Tmp6,Tmp7,Tmp8,Tmp9)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
				Tmp6 = ""
				Tmp7 = ""
				Tmp8 = ""
				Tmp9 = ""
			Next	
			For Each Parameters In elem.selectNodes("SetRegIfVar")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "Var" 		: Tmp1 = SubItem.Text
						Case "Op" 		: Tmp2 = SubItem.Text
						Case "Value" 	: Tmp3 = SubItem.Text
						Case "RegKey" 	: Tmp4 = SubItem.Text
						Case "RegValue" : Tmp5 = SubItem.Text
						Case "RegType" 	: Tmp6 = SubItem.Text
						Case "WTS" 		: Tmp7 = SubItem.Text
						Case "PC" 		: Tmp8 = SubItem.Text
					End Select
				Next
				Call LoginSetRegIfVarFunction(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5,Tmp6,Tmp7,Tmp8)			
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
				Tmp6 = ""
				Tmp7 = ""		
				Tmp8 = ""
			Next	
			For Each Parameters In elem.selectNodes("Create")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "Folder" 	
							Tmp1 = SubItem.Text
							Tmp2 = SubItem.nodeName
						Case "File" 	
							Tmp1 = SubItem.Text
							Tmp2 = SubItem.nodeName
						Case "Shortcut"
							Tmp1 = SubItem.Text
							Tmp2 = SubItem.nodeName
						Case "OverWrite"	: Tmp3 = SubItem.Text
						Case "WTS" 			: Tmp4 = SubItem.Text
						Case "PC" 			: Tmp5 = SubItem.Text
						Case "Cmd" 			: Tmp6 = SubItem.Text
						Case "WorkDir" 		: Tmp7 = SubItem.Text
						Case "Icon" 		: Tmp8 = SubItem.Text
						Case "Location" 	: Tmp9 = SubItem.Text
						Case "HotKey" 		: Tmp10 = SubItem.Text
						Case "WindowStyle"	: Tmp11 = SubItem.Text
					End Select
				Next
				Call CreateFunction(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5,Tmp6,Tmp7,Tmp8,Tmp9,Tmp10,Tmp11)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
				Tmp6 = ""
				Tmp7 = ""
				Tmp8 = ""
				Tmp9 = ""
				Tmp10 = ""
				Tmp11 = ""
			Next				
			For Each Parameters In elem.selectNodes("CopyFolder")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "Folder" 		: Tmp1 = SubItem.Text
						Case "To"	 		: Tmp2 = SubItem.Text
						Case "WTS" 			: Tmp3 = SubItem.Text
						Case "PC" 			: Tmp4 = SubItem.Text
					End Select
				Next
				Call LoginDoCopyFolder(Tmp1,Tmp2,Tmp3,Tmp4)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
			Next
			For Each Parameters In elem.selectNodes("Copy")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "File" 		: Tmp1 = SubItem.Text
						Case "To"	 		: Tmp2 = SubItem.Text
						Case "OverWrite"	: Tmp3 = SubItem.Text
						Case "WTS" 			: Tmp4 = SubItem.Text
						Case "PC" 			: Tmp5 = SubItem.Text
					End Select
				Next
				Call LoginDoCopy(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
			Next
			For Each Parameters In elem.selectNodes("RunIfVar")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "Var" 		: Tmp1 = SubItem.Text
						Case "Op" 		: Tmp2 = SubItem.Text
						Case "Value" 	: Tmp3 = SubItem.Text
						Case "Do" 		: Tmp4 = SubItem.Text
						Case "DoArg" 	: Tmp5 = SubItem.Text						
						Case "ElseDo" 	: Tmp6 = SubItem.Text
						Case "ElseDoArg" : Tmp7 = SubItem.Text						
						Case "Wait" 	: Tmp8 = SubItem.Text
						Case "Visible" 	: Tmp9 = SubItem.Text
						Case "WTS" 		: Tmp10 = SubItem.Text
						Case "PC" 		: Tmp11 = SubItem.Text
						Case "Bits"		: Tmp12 = SubItem.Text
					End Select
				Next
				Call LoginRunIfVarFunction(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5,Tmp6,Tmp7,Tmp8,Tmp9,Tmp10,Tmp11,Tmp12)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
				Tmp6 = ""
				Tmp7 = ""		
				Tmp8 = ""
				Tmp9 = ""
				Tmp10 = ""
				Tmp11 = ""	
				Tmp12 = ""
			Next			
			Call ParseAndRunProgram(elem, "RunProgram")
			For Each Parameters In elem.selectNodes("RunIf")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "What" 	: Tmp1 = SubItem.Text
						Case "Item" 	: Tmp2 = SubItem.Text
						Case "Op" 		: Tmp3 = SubItem.Text
						Case "Value" 	: Tmp4 = SubItem.Text
						Case "Do" 		: Tmp5 = SubItem.Text
						Case "DoArg" 	: Tmp6 = SubItem.Text
						Case "ElseDo" 	: Tmp7 = SubItem.Text
						Case "ElseDoArg" : Tmp8 = SubItem.Text
						Case "Wait" 	: Tmp9 = SubItem.Text
						Case "Visible" 	: Tmp10 = SubItem.Text
						Case "WTS" 		: Tmp11 = SubItem.Text
						Case "PC" 		: Tmp12 = SubItem.Text
						Case "Bits"		: Tmp13 = SubItem.Text
					End Select
				Next
				Call LoginRunIfFunction(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5,Tmp6,Tmp7,Tmp8,Tmp9,Tmp10,Tmp11,Tmp12,Tmp13)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
				Tmp6 = ""
				Tmp7 = ""		
				Tmp8 = ""
				Tmp9 = ""
				Tmp10 = ""
				Tmp11 = ""
				Tmp12 = ""
				Tmp13 = ""
			Next
			Call ParseAndRunProgram(elem, "RunProgram1")
			Call ParseAndRunProgram(elem, "RunProgram2")
			For Each Parameters In elem.selectNodes("Sleep")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "TimeOut" 	: Tmp1 = SubItem.Text
					End Select
				Next
				Call Delay(GetVal(Tmp1))
				Tmp1 = ""
			Next	
			For Each Parameters In elem.selectNodes("PopupMessage")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "Text" 		: Tmp1 = SubItem.Text
						Case "Caption" 		: Tmp2 = SubItem.Text
						Case "TimeOut" 		: Tmp3 = SubItem.Text
						Case "WTS" 			: Tmp4 = SubItem.Text
						Case "PC" 			: Tmp5 = SubItem.Text
					End Select
				Next
				Call LoginDoMessage(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
			Next 
			For Each Parameters In elem.selectNodes("EndProgress")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "WTS" 			: Tmp1 = SubItem.Text
						Case "PC" 			: Tmp2 = SubItem.Text
					End Select
				Next
				Call LoginDoEndProgress(Tmp1,Tmp2)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
			Next
			For Each Parameters In elem.selectNodes("PopupFinishMessage")
				For Each SubItem In Parameters.Attributes
					Select Case SubItem.nodeName
						Case "Text" 		: Tmp1 = SubItem.Text
						Case "Caption" 		: Tmp2 = SubItem.Text
						Case "TimeOut" 		: Tmp3 = SubItem.Text
						Case "WTS" 			: Tmp4 = SubItem.Text
						Case "PC" 			: Tmp5 = SubItem.Text
					End Select
				Next
				Call LoginDoMessage(Tmp1,Tmp2,Tmp3,Tmp4,Tmp5)
				Tmp1 = ""
				Tmp2 = ""
				Tmp3 = ""
				Tmp4 = ""
				Tmp5 = ""
			Next
		End if		
	Next
End Sub

' MAIN LINE 
Dim xmlDoc
Dim ElemList
Dim elem
Dim itm
Dim DOMAINNAME_GLOBAL
Dim strScriptName
Dim strDebug
Dim WINDIR_GLOBAL
Dim TEMPFOLDER_GLOBAL
Dim WTSSESSIONNAME_GLOBAL
Dim CURRENTUSER_GLOBAL
Dim WshShell
Dim WshProcessEnvironment
Dim USERPROFILE_GLOBAL
Dim COMPUTERNAME_GLOBAL
Dim ALLUSERSPROFILE_GLOBAL
Dim APPDATA_GLOBAL
Dim SYSTEMDRIVE_GLOBAL
Dim PUBLICFOLDER_GLOBAL
Dim PROGRAMW6432_GLOBAL
Dim PROGRAMFILES_GLOBAL
Dim PROGRAMDATA_GLOBAL
Dim COMSPEC_GLOBAL
Dim LOCALAPPDATA_GLOBAL
Dim HOMEUNC_GLOBAL
Dim HOMEPATH_GLOBAL
Dim HOMEDRIVELETTER_GLOBAL
Dim WTSENV
Dim PCENV
Dim USERDISPLAYNAME_GLOBAL
Dim ADGroupList
Dim TermedUsers
Dim RunOrder
Dim RunNode
Dim LOGONSERVER_GLOBAL
Dim LOGFILE_GLOBAL
Dim XMLERR
Dim XMLFile
Dim CURRENTWORKINGFOLDER_GLOBAL
Dim BITNESS_GLOBAL      ' OPT: Replaces BITNESS64; caches CPU bitness once for Is32/64BitOS helpers
Dim OSVERSIONMAJORMINOR_GLOBAL
ADGroupList = ""
Const ADS_READONLY_SERVER = 4
Set WshShell = CreateObject("WScript.Shell")
CURRENTWORKINGFOLDER_GLOBAL = WshShell.CurrentDirectory
' OPT: Cache CPU bitness once; Is32BitOS() / Is64BitOS() read this variable
' instead of hitting WMI on every call.
BITNESS_GLOBAL = GetObject("winmgmts:root\cimv2:Win32_Processor='cpu0'").AddressWidth
Set WshProcessEnvironment = WshShell.Environment("Process")
WINDIR_GLOBAL = WshProcessEnvironment("windir")
LOGONSERVER_GLOBAL = WshProcessEnvironment("LOGONSERVER")
TEMPFOLDER_GLOBAL = WshProcessEnvironment("TEMP")
WTSSESSIONNAME_GLOBAL = WshProcessEnvironment("SESSIONNAME")
CURRENTUSER_GLOBAL = WshProcessEnvironment("USERNAME")
USERPROFILE_GLOBAL = WshProcessEnvironment("USERPROFILE")
COMPUTERNAME_GLOBAL = WshProcessEnvironment("COMPUTERNAME")
HOMEDRIVELETTER_GLOBAL = WshProcessEnvironment("HOMEDRIVE")
HOMEPATH_GLOBAL = WshProcessEnvironment("HOMEPATH")
HOMEUNC_GLOBAL = WshProcessEnvironment("HOMESHARE")
LOCALAPPDATA_GLOBAL = WshProcessEnvironment("LOCALAPPDATA")
COMSPEC_GLOBAL = WshProcessEnvironment("ComSpec")
PROGRAMDATA_GLOBAL = WshProcessEnvironment("ProgramData")
PROGRAMFILES_GLOBAL = WshProcessEnvironment("ProgramFiles")
PROGRAMW6432_GLOBAL = WshProcessEnvironment("ProgramW6432")
PUBLICFOLDER_GLOBAL = WshProcessEnvironment("PUBLIC")
SYSTEMDRIVE_GLOBAL = WshProcessEnvironment("SystemDrive")
APPDATA_GLOBAL = WshProcessEnvironment("APPDATA")
ALLUSERSPROFILE_GLOBAL = WshProcessEnvironment("ALLUSERSPROFILE")
DOMAINNAME_GLOBAL = WshProcessEnvironment("USERDOMAIN")
OSVERSIONMAJORMINOR_GLOBAL = FindOSVersionMajorMinor

'Get AD Info of User
ADGroupList = GetADGroups(DOMAINNAME_GLOBAL, CURRENTUSER_GLOBAL)

LOGFILE_GLOBAL = ""
XMLFile = "XML_Tasks.xml" 'Resolve(LOGONSERVER_GLOBAL & "\netlogon\XMLLoginTasks.xml")
WTSENV = (trim(WTSSESSIONNAME_GLOBAL) <> "")   ' OPT: Collapsed two If-lines into direct Boolean assignment
PCENV  = Not WTSENV

Set xmlDoc = CreateObject("Msxml2.DOMDocument")
xmlDoc.load(XMLFile)
If xmlDoc.parseError.errorCode <> 0 Then
   Set XMLERR = xmlDoc.parseError
   MsgBox "XML Error: " & XMLERR.errorCode & "  " & XMLERR.reason & " Line=" & XMLERR.line & " in XMLFile=" & XMLFile
Else
	Set ElemList = xmlDoc.getElementsByTagName("Start")
	For Each elem In ElemList
		For Each itm In elem.Attributes	
			Select Case itm.nodeName
				Case "Name" 	: strScriptName = itm.Text
				Case "Order" 	: RunOrder = split(itm.Text,";")
				Case "BumpOff" 	: TermedUsers = itm.text
				Case "Debug" 	: strDebug = TF(itm.text)
				Case "LogFile"	: LOGFILE_GLOBAL = Resolve(itm.text)			
			End Select
		Next
	Next
	md(pathname(LOGFILE_GLOBAL))
	call LogItToFile(LOGFILE_GLOBAL,"========|========|========")
	call LogItToFile(LOGFILE_GLOBAL,strScriptName & " Debug=" & strDebug & " | UserName=" & CURRENTUSER_GLOBAL & " USERPROFILE=" & USERPROFILE_GLOBAL & " TEMP=" & TEMPFOLDER_GLOBAL & " HOMESHARE=" & HOMEUNC_GLOBAL & " | " & XMLERR)
	If instr(1,ADGroupList,TermedUsers,1) > 0 then 
		Call KillSession()
	Else
		For each RunNode in RunOrder
			Call DoSteps(xmlDoc, RunNode,ADGroupList,CURRENTUSER_GLOBAL)
		next
	End If
End IF
	' Glenn Madine
' 
'  \_(?)_/ 
'
'
'===