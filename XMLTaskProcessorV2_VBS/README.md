# XMLTaskProcessorV2.vbs — Technical Reference

## Overview

`XMLTaskProcessorV2.vbs` is a Windows login/startup script written in VBScript. It reads an XML task file (`XML_Tasks.xml`) and executes a configurable set of actions — drive mappings, registry edits, file copies, program launches, environment variable changes, and more — targeting specific users, AD groups, or computer names.

It is designed to run in two environments:
- **WTS (Windows Terminal Services / RDS)** — remote/session logon
- **PC** — standard desktop logon

Each XML task can be restricted to one or both environments using `WTS="true/false"` and `PC="true/false"` attributes.

---

## Architecture

```
Startup
  └── Load environment globals (user, computer, paths, AD groups)
  └── Parse XML_Tasks.xml
  └── Read <Start> element for config (order, log file, bump-off list)
  └── Loop through RunOrder nodes
        └── DoSteps(xmlDoc, Context, ADGroupList, CurrentUser)
              └── Match elem by group / computer / user / "Global"
              └── Execute each child task element in order
```

---

## Global Variables (set at startup)

| Variable | Source | Description |
|---|---|---|
| `CURRENTUSER_GLOBAL` | `%USERNAME%` | Logged-in username |
| `COMPUTERNAME_GLOBAL` | `%COMPUTERNAME%` | Machine name |
| `USERPROFILE_GLOBAL` | `%USERPROFILE%` | User profile root path |
| `TEMPFOLDER_GLOBAL` | `%TEMP%` | Temp folder path |
| `DOMAINNAME_GLOBAL` | `%USERDOMAIN%` | AD domain name |
| `LOGONSERVER_GLOBAL` | `%LOGONSERVER%` | DC the user authenticated against |
| `APPDATA_GLOBAL` | `%APPDATA%` | Roaming AppData path |
| `LOCALAPPDATA_GLOBAL` | `%LOCALAPPDATA%` | Local AppData path |
| `PROGRAMFILES_GLOBAL` | `%ProgramFiles%` | 64-bit Program Files |
| `PROGRAMW6432_GLOBAL` | `%ProgramW6432%` | Native 64-bit Program Files |
| `PROGRAMDATA_GLOBAL` | `%ProgramData%` | All-users application data |
| `PUBLICFOLDER_GLOBAL` | `%PUBLIC%` | Public user folder |
| `SYSTEMDRIVE_GLOBAL` | `%SystemDrive%` | Drive containing Windows |
| `HOMEUNC_GLOBAL` | `%HOMESHARE%` | UNC home share |
| `HOMEDRIVELETTER_GLOBAL` | `%HOMEDRIVE%` | Home drive letter |
| `HOMEPATH_GLOBAL` | `%HOMEPATH%` | Home path |
| `COMSPEC_GLOBAL` | `%ComSpec%` | Path to cmd.exe |
| `ALLUSERSPROFILE_GLOBAL` | `%ALLUSERSPROFILE%` | Legacy all-users profile |
| `WINDIR_GLOBAL` | `%windir%` | Windows directory |
| `BITNESS_GLOBAL` | WMI `Win32_Processor.AddressWidth` | 32 or 64 — cached once at startup |
| `WTSENV` | Derived | `True` if running in a terminal/RDS session |
| `PCENV` | Derived | `True` if running on a physical PC (not WTS) |
| `LOGFILE_GLOBAL` | XML `<Start LogFile="...">` | Path to the script's log file |
| `OSVERSIONMAJORMINOR_GLOBAL` | WMI `Win32_OperatingSystem` | OS version string, e.g. `10.0` |
| `ADGroupList` | `GetADGroups()` | Pipe-delimited list of user's AD groups |

---

## Path Shorthand Tokens (`Resolve` function)

Within XML attribute values, use these tokens instead of hardcoded paths:

| Token | Expands to |
|---|---|
| `~UP` | `USERPROFILE_GLOBAL` |
| `~UN` | `CURRENTUSER_GLOBAL` |
| `~UT` | `TEMPFOLDER_GLOBAL` |
| `~CN` | `COMPUTERNAME_GLOBAL` |
| `~AD` | `APPDATA_GLOBAL` |
| `~CS` | `COMSPEC_GLOBAL` |
| `~DN` | `DOMAINNAME_GLOBAL` |
| `~HS` | `HOMEUNC_GLOBAL` |
| `~PD` | `PROGRAMDATA_GLOBAL` |
| `~P6` | `PROGRAMFILES_GLOBAL` |
| `~P3` | `PROGRAMW6432_GLOBAL` |
| `~PU` | `PUBLICFOLDER_GLOBAL` |
| `~SY` | `SYSTEMDRIVE_GLOBAL` |
| `~AU` | `ALLUSERSPROFILE_GLOBAL` |
| `~LA` | `LOCALAPPDATA_GLOBAL` |
| `~HL` | `HOMEDRIVELETTER_GLOBAL` |
| `~HP` | `HOMEPATH_GLOBAL` |
| `~CD` | `CURRENTWORKINGFOLDER_GLOBAL` |
| `~UD` | `USERDISPLAYNAME_GLOBAL` |

**Example:** `File="~UP\Desktop\readme.txt"` → `C:\Users\jdoe\Desktop\readme.txt`

---

## XML Task File Schema

### `<Start>` — top-level configuration

```xml
<Start
  Name="My Login Script"
  Order="Global;SomeGroup;AnotherGroup"
  BumpOff="TermedUsersGroup"
  Debug="true"
  LogFile="~HS\logs\login.log"
/>
```

| Attribute | Description |
|---|---|
| `Name` | Descriptive name logged at startup |
| `Order` | Semicolon-delimited list of XML context names to process, in order |
| `BumpOff` | AD group name — members are immediately logged off |
| `Debug` | Enables debug output (`true`/`false`) |
| `LogFile` | Log file path (supports `~` tokens) |

---

### Context Elements — task grouping

Tasks are grouped under elements named after the context. The script matches a context element if:

- **`Global`** — always runs
- **`<GroupName>`** — runs if the user belongs to that AD group
- **`<UserName>`** — runs if the username matches exactly
- **`<Computer Name="PC01">`** — runs if the computer name matches (supports `*` wildcard prefix, e.g. `Name="SALES*"`)

---

### Task Elements (child of a context)

#### `<RunProgram>` — run an executable

```xml
<RunProgram CMD="notepad.exe" Arg="~UP\notes.txt"
  Wait="true" Visible="true" WTS="true" PC="true" Bits="32,64"/>
```

| Attribute | Values | Description |
|---|---|---|
| `CMD` | Path or exe name | Program to run |
| `Arg` | String | Command-line arguments |
| `Wait` | `true`/`false` | Wait for process to exit before continuing |
| `Visible` | `true`/`false` | Show window (`true`) or run hidden (`false`) |
| `WTS` | `true`/`false` | Run in terminal sessions |
| `PC` | `true`/`false` | Run on physical PCs |
| `Bits` | `32`, `64`, `32,64` | Restrict to 32-bit or 64-bit OS |

Also available: `<RunProgram1>` and `<RunProgram2>` — identical syntax, executed in sequence order within the same context.

---

#### `<RunIf>` — conditional program execution

```xml
<RunIf What="FileExists" Item="~UP\marker.txt" Op="eq" Value="true"
  Do="setup.exe" DoArg="/silent"
  ElseDo="fallback.exe" ElseDoArg=""
  Wait="true" Visible="false" WTS="true" PC="true" Bits="64"/>
```

| Attribute | Description |
|---|---|
| `What` | Name of a VBScript function to call (e.g. `FileExists`, `FolderExists`, `GetFileSize`) |
| `Item` | Argument passed to that function |
| `Op` | Comparison: `eq`, `neq`, `gt`, `lt`, `geq`, `leq`, `in`, `notin` |
| `Value` | Value to compare the function's result against |
| `Do` / `DoArg` | Program to run if condition is true |
| `ElseDo` / `ElseDoArg` | Program to run if condition is false (optional) |

---

#### `<RunIfVar>` — run based on environment variable value

```xml
<RunIfVar Var="APPENV" Op="eq" Value="PROD"
  Do="prodsetup.exe" DoArg="" ElseDo="" ElseDoArg=""
  Wait="true" Visible="false" WTS="false" PC="true" Bits="32,64"/>
```

Same as `<RunIf>` but tests an environment variable by name rather than calling a function.

---

#### `<Map>` — map drive or printer

```xml
<!-- Drive mapping -->
<Map Drive="H:" UNC="\\server\share" ForceMap="true"
  Persistent="false" WTS="true" PC="true"
  DelayBefore="0" DelayAfter="0"/>

<!-- Printer mapping -->
<Map Printer="\\printserver\HP_Color" UNC="\\printserver\HP_Color"
  Default="true" WTS="true" PC="false"/>
```

| Attribute | Description |
|---|---|
| `Drive` / `Printer` | Drive letter (e.g. `H:`) or UNC printer path |
| `UNC` | Network share path |
| `ForceMap` | Disconnect and remap even if already mapped |
| `Persistent` | Survive logoff (`true`/`false`) |
| `Default` | Set as default printer (`true`/`false`) |
| `DelayBefore` / `DelayAfter` | Milliseconds to wait before/after mapping |

---

#### `<Reg>` — read/write/delete registry values

```xml
<Reg Key="HKCU\Software\MyApp" Value="Setting" Data="1"
  Type="REG_DWORD" Op="update" PC="true" WTS="true"/>
```

| Attribute | Values | Description |
|---|---|---|
| `Key` | Registry path | Full registry key path |
| `Value` | String | Value name |
| `Data` | String | Data to write (ignored for `delete`) |
| `Type` | `REG_SZ`, `REG_DWORD`, `REG_EXPAND_SZ`, `REG_BINARY`, `REG_MULTI_SZ` | Registry value type |
| `Op` | `update`, `add`, `delete` | Operation to perform |

---

#### `<SetRegIfVar>` — conditionally write a registry value

```xml
<SetRegIfVar Var="USERDOMAIN" Op="eq" Value="CORP"
  RegKey="HKCU\Software\Corp" RegValue="Enabled"
  RegType="REG_DWORD" WTS="true" PC="true"/>
```

Writes the registry value only if the named environment variable matches the comparison.

---

#### `<SetEnv>` — set a user environment variable

```xml
<SetEnv Var="MY_APP_ROOT" Value="~P6\MyApp" WTS="true" PC="true"/>
```

---

#### `<AddPath>` — append to the user PATH

```xml
<AddPath Path="~P6\MyApp\bin" WTS="true" PC="true"/>
```

Appends the specified folder to the user-level `PATH` environment variable if not already present.

---

#### `<Copy>` — copy a file

```xml
<Copy File="\\server\share\config.ini" To="~UP\AppData\MyApp\"
  OverWrite="true" WTS="true" PC="true"/>
```

---

#### `<CopyFolder>` — copy a folder tree

```xml
<CopyFolder Folder="\\server\share\templates" To="~UP\Documents"
  WTS="true" PC="true"/>
```

---

#### `<Create>` — create a folder, file, or shortcut

```xml
<!-- Folder -->
<Create Folder="~UP\MyApp\Logs" WTS="true" PC="true"/>

<!-- Empty file -->
<Create File="~UP\MyApp\marker.txt" OverWrite="false" WTS="true" PC="true"/>

<!-- Shortcut -->
<Create Shortcut="My App" Location="~UP\Desktop\My App.lnk"
  Cmd="~P6\MyApp\app.exe" WorkDir="~P6\MyApp"
  Icon="~P6\MyApp\app.exe,0" HotKey="" WindowStyle="1"
  WTS="true" PC="true"/>
```

---

#### `<ShowProgress>` / `<EndProgress>` — progress display

```xml
<ShowProgress Text="Configuring your environment..." Banner="IT Setup"
  Type="1" WTS="true" PC="true"/>
<!-- ... tasks ... -->
<EndProgress WTS="true" PC="true"/>
```

Launches an HTA progress indicator (`ShowProgress1.hta` etc.) and terminates it. The `Type` attribute selects the HTA variant.

---

#### `<PopupMessage>` / `<PopupMessageFirst>` / `<PopupFinishMessage>` — user dialogs

```xml
<PopupMessage Text="Setup is complete. Please restart."
  Caption="IT Notice" TimeOut="30" WTS="true" PC="true"/>
```

`PopupMessageFirst` runs before most other tasks; `PopupFinishMessage` runs at the end. `TimeOut` in seconds (0 = no timeout).

---

#### `<ClearProcess>` — prompt user to close running applications

```xml
<ClearProcess
  ProcList="outlook.exe|Microsoft Outlook,teams.exe|Microsoft Teams"
  Caption="Please close applications" ElseMsg="Click Cancel to abort."
  ForProcName="MyApp Setup" WTS="true" PC="true"/>
```

`ProcList` is a comma-delimited list of `process.exe|Friendly Name` pairs. The user is shown a dialog listing any that are running and asked to close them before continuing. Cancelling exits the script with code `777`.

---

#### `<Sleep>` — pause execution

```xml
<Sleep TimeOut="3000"/>
```

Pauses for the given number of milliseconds.

---

## Utility Functions Reference

### File / Folder

| Function | Returns | Description |
|---|---|---|
| `FileExists(path)` | Boolean | True if file exists |
| `FolderExists(path)` | Boolean | True if folder exists |
| `GetFileSize(path)` | Long | File/folder size in bytes, or -1 if missing |
| `FileCreated(path)` | Date | File creation date |
| `FileLastModified(path)` | Date | Last modified date |
| `FileLastAccessed(path)` | Date | Last accessed date |
| `FileAge(path)` | Double | Age in days since last modification |
| `LinesInFile(path)` | Integer | Total line count |
| `fRead(path)` | String | Full file contents (ANSI or Unicode) |
| `Head(path, n)` | String | First `n` lines |
| `Tail(path, n)` | String | Last `n` lines (single-pass circular buffer) |
| `VerFile(path)` | String | File version number |
| `IsUnicodeFile(path)` | Boolean | True if file starts with Unicode BOM |
| `IsHiddenFile(path)` | Boolean | True if hidden attribute is set |
| `isFileAttribute(path, attr)` | Boolean | Test `hidden`, `system`, `readonly`, `archive`, `link`, `compressed` |
| `ShowFileAccessInfo(path)` | String | Formatted file metadata summary |

### Folder

| Function | Returns | Description |
|---|---|---|
| `FolderCreated(path)` | Date | Folder creation date |
| `FolderSize(path)` | Long | Total folder size in bytes |
| `FolderSet(path)` | Array | All subfolders recursively |
| `LargestNumberedSubFolder(path)` | Integer | Highest-numbered direct subfolder |
| `folIsEmpty(path)` | String | `"True"` if folder has no files |
| `HasRights(path)` | String | `"True"` if user has list rights |
| `IsHiddenFolder(path)` | Boolean | True if hidden attribute is set |
| `isFolderAttribute(path, attr)` | Boolean | Test `hidden`, `system`, `readonly`, `folder`, `archive`, `compressed` |
| `DeleteIfEmpty(path)` | — | Deletes folder if it contains nothing |

### File I/O

| Sub/Function | Description |
|---|---|
| `fWrite(path, data)` | Overwrite file with data |
| `fAppend(path, data)` | Append data to file |
| `WriteFile(path, data, type)` | Write with explicit encoding (`ASCII` or `UNICODEFILE`) |
| `AppendFile(path, data, type)` | Append with explicit encoding |
| `ReadFile(path, type)` | Read with explicit encoding |
| `NewFile(path, overwrite)` | Create empty file |
| `DeleteFile(path)` | Delete file if it exists |
| `LogItToFile(path, msg)` | Append timestamped log entry |
| `TrimFileSize(path, size)` | Trim oldest lines until file is under `size` bytes |

### Path Helpers

| Function | Returns | Description |
|---|---|---|
| `Basename(path)` | String | Filename portion of a full path |
| `Pathname(path)` | String | Directory portion of a full path |
| `FileExt(path)` | String | Extension (without dot) |
| `GetWinDir()` | String | Windows directory |
| `GetWinSys()` | String | Windows\System32 directory |
| `GetWinTemp()` | String | Windows temp directory |
| `GetTempName()` | String | Random temp filename |
| `GetTempFullPath()` | String | Full path to a random temp file |
| `TempFileName()` | String | Alias for `GetTempName` |
| `TempFileBase()` | String | Full path using a random 8-char name |

### Drives & Disks

| Function/Sub | Description |
|---|---|
| `GetDiskFree(drive)` | KB free on a drive |
| `ShowDriveType(drive)` | Human-readable drive type string |
| `CdRomDrives()` | Array of CD-ROM drive letters |
| `GetRemovableDrive()` | First removable drive path |
| `FreeDrive(first, last)` | First available drive letter in range |
| `ValidDriveLetter(drive)` | True if letter is D–Z |
| `Map(drive, unc, persist)` | Map a network drive |
| `ForceDriveReMap(drive, unc, persist)` | Disconnect then remap |
| `NTFSCompressFolder(path)` | NTFS-compress a folder |
| `NTFSCompressFile(path)` | NTFS-compress a single file |

### Registry

| Function/Sub | Description |
|---|---|
| `RegExists(key)` | True if registry key/value exists |
| `GetRegVal(key)` | Read a registry value |
| `UpdateReg(key, value, data, type)` | Write a registry value |
| `DeleteReg(item)` | Delete a registry key or value |
| `ExportReg(key, file)` | Export registry branch to `.reg` file |
| `ImportReg(file)` | Import a `.reg` file |

### Process / Execution

| Function/Sub | Description |
|---|---|
| `RunProgram(cmd, wait)` | Run a program in a visible window |
| `RunProgramHidden(cmd, wait)` | Run a program hidden |
| `ExecProgram(cmd)` | Run and return process ID |
| `ExecProgramCaptureOutput(cmd)` | Run and return stdout + exit code |
| `ExecProgramCaptureDelimitedOutput(cmd, delim, type)` | Run and return delimited output |
| `KillProcess(name)` | Terminate all processes matching name |
| `KillSession()` | Force logoff current session |
| `QuitNow(code)` | Exit script with given code |

### Environment

| Function/Sub | Description |
|---|---|
| `GetEnv(var)` | Expand an environment variable |
| `SaveLocalVar(name, value)` | Set a user-level environment variable |
| `GetLocalEnv(name)` | Read a user-level environment variable |
| `AddPathLocal(folder)` | Append folder to user PATH |

### OS / System

| Function | Description |
|---|---|
| `Is64BitOS()` | True if running on 64-bit OS (cached) |
| `Is32BitOS()` | True if running on 32-bit OS (cached) |
| `FindOSVersionMajorMinor()` | Returns e.g. `"10.0"` |
| `FindOSVersionFull()` | Returns full version string |
| `IsRunningAsAdministrator()` | True if running elevated |
| `IsDcUp(server)` | True if domain controller is reachable |
| `IsShare(path)` | True if UNC/disk path is accessible |
| `GetADGroups(domain, user)` | Pipe-delimited AD group list |

### Printing

| Sub | Description |
|---|---|
| `MapPrinter(unc, default)` | Connect to a network printer |
| `SetDefPrinter(unc)` | Set default printer |
| `DeletePrinter(unc)` | Remove printer connection |
| `DetectLocalPrinter(name)` | True if named local printer exists |

### Miscellaneous

| Function/Sub | Description |
|---|---|
| `TF(x)` | Returns `True` for `"true"`, `"1"`, `"yes"`, `"on"` |
| `GetVal(x)` | Returns `1000` for yes/on/1, `0` for no/off/0, else `CInt(x)` |
| `Resolve(item)` | Expands `~XX` path tokens |
| `Evalf(func, item, op, val)` | Evaluates `func(item) op val` |
| `EnvMatch(wts, pc)` | True if current environment matches WTS/PC flags |
| `Delay(ms)` | Sleep for `ms` milliseconds |
| `Chop(str)` | Remove last character |
| `ChopChar(str, char)` | Remove last character if it matches `char` |
| `ChopChop(str, before, after)` | Trim before/after delimiters |
| `RandomNumb(hi, lo)` | Random integer between `lo` and `hi` |
| `ElementsInArray(arr)` | Total element count |
| `NonEmptyElementsInArray(arr)` | Count of non-empty elements |
| `IsDST(date, arr)` | Returns 1 if date is in DST, 0 if not |
| `LowerCaseFSO(path)` | Lowercase all filenames in a folder tree |
| `XCopy(from, to)` | xcopy with `/S /Y` flags |
| `CopyFolder(src, dest)` | Copy folder if destination doesn't exist |
| `Copy(from, to, overwrite)` | Copy file(s) |
| `MoveFile(old, new)` | Move/rename a file |
| `md(path)` | Recursively create directory path |
| `HideFolder(path)` / `UnHideFolder(path)` | Set/clear hidden attribute on folder |
| `HideFile(path)` / `UnHideFile(path)` | Set/clear hidden attribute on file |
| `SetReadOnlyFile(path)` / `UnReadOnlyFile(path)` | Set/clear read-only attribute |
| `IncludeVbsScript(path)` | Execute another `.vbs` file in global context |
| `FreeDrive(first, last)` | First unused drive letter in range |
| `CreateShortCut(...)` | Create a Windows shortcut (.lnk) |
| `RemoveFoldersOverNDays(path, n)` | Delete folders/files older than n days |
| `LogEvent(type, msg)` | Write to Windows Event Log |

---

## Execution Flow Summary

1. **Global initialization** — environment variables, AD groups, OS bitness cached.
2. **XML load** — `XML_Tasks.xml` is parsed. Parse errors show a message box.
3. **`<Start>` config read** — script name, run order, log file, terminated-user group.
4. **Log directory created** — `md()` ensures the log path exists.
5. **BumpOff check** — if user is in `TermedUsers` AD group, `KillSession()` is called.
6. **Task loop** — for each node in `RunOrder`, `DoSteps()` is called, which:
   - Iterates all matching context elements (Global, group, user, computer)
   - Executes every child task element in document order
   - Applies `WTS`/`PC` and `Bits` filters to each task
   - Resolves `~XX` tokens in all path values before use

---

## Key Design Notes

- **All actions are logged** to `LOGFILE_GLOBAL` via `LogItToFile()`.
- **`Resolve()` is called on all path-bearing attributes** before use — always use `~` tokens in XML rather than hardcoded paths.
- **`TF()` normalizes boolean XML attributes** — `"yes"`, `"1"`, `"on"`, and `"true"` all mean `True`.
- **`BITNESS_GLOBAL` is populated once** at startup via WMI; `Is32BitOS()` / `Is64BitOS()` are cheap reads thereafter.
- **`Tail()` uses a circular buffer** — single file pass regardless of line count.
- **`FolderSet()` recurses correctly** — a prior bug (double-incrementing loop index) has been fixed.
- **`LogItToFile()` auto-creates the log file** if absent — no pre-creation step needed.
