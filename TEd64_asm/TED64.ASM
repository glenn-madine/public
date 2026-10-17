;======================================================================
;  TED64.ASM -- The Tiny EDitor, x64 Windows console version.
;
;  Original: TED.ASM, PC Magazine * Tom Kihlken * 11/15/88 (revised)
;
;  A port of the 16-bit DOS .COM editor to a 64-bit Windows console
;  application for Microsoft MASM (ml64.exe / Visual Studio).  The key
;  layout and the overall structure follow the original.
;
;  Version 2.0:
;    * Wide (CJK, fullwidth, emoji) characters take two cells, and
;      characters above U+FFFF are drawn as surrogate pairs.
;    * Unicode file names, on the command line and at "Save as".
;    * The view scrolls sideways by itself to keep the cursor in sight.
;    * Undo (F2 or Ctrl-Z) and redo (Ctrl-Y) of every change, as far
;      back as the file was opened (256 MB of history).  A run of typing
;      or erasing is one step.  The "*" goes when the text matches the
;      saved file again.
;    * Find folds the case of Latin, Greek and Cyrillic letters, takes a
;      regular expression (Ctrl-R in the prompt), and Replace is on
;      Ctrl-H (Y/N/A for each match).
;    * Tab width set with /T:n (1-32, default 8).
;    * Hard-linked files, and drives without ReplaceFile, are saved in
;      place after the old version is copied to .BAK.
;    * Closing the window, logging off or shutting down with unsaved
;      changes writes the text to name.SAV, reported at the next start.
;    * A change made to the file by another program is detected by its
;      identity and a hash of its contents, not just its time and size.
;    * Every procedure has an RBP frame and Win64 unwind data, so
;      debuggers and crash dumps can walk the stack.
;    * The largest file is 4 GB (FILE_MAX), down from 64 GB.
;    * Ctrl-X = CUT (F5), Ctrl-V = PASTE (F6), Ctrl-Q = EXIT (F7).
;    * CUT and COPY also put the text on the Windows clipboard, and
;      PASTE takes text other programs put there (needs user32.lib).
;
;  Version 1.6:
;    * A file that exists but can't be opened (in use, access denied,
;      a folder) is reported instead of being treated as a new, empty
;      file.  Files are opened allowing other programs to keep writing.
;    * Ctrl-Break is ignored (a control handler), so it can't end TED
;      and lose unsaved text.
;    * Saving warns if another program has changed the file since it
;      was loaded or last saved.
;    * A read-only or undeletable .BAK file is reported, not just beeped.
;    * Exit code 1 after an error, 0 otherwise.
;
;  Version 1.5:
;    * Draws on a screen buffer of its own, so the user's console
;      contents reappear untouched on exit.
;    * Saves with ReplaceFile, keeping the file's permissions (ACLs),
;      attributes, alternate data streams and creation time.  Read-only
;      files are not overwritten.
;    * If the system limits address space, smaller buffers are reserved
;      instead of failing at startup.
;    * Procedures no longer jump into the middle of other procedures,
;      and unassigned keys no longer leave a return address on the stack.
;
;  Version 1.4 added SAVE on F10: save the file (name editable, Esc
;  cancels) and carry on editing.  UDEL L (undelete line) moved to F12.
;  The prompt line drops its spaces when the window is too narrow.
;
;  Version 1.3 added:
;    * Gap buffer.  Text is held in one buffer with a movable gap at
;      the last edit point, so typing costs the same anywhere in the
;      file, even in files of many GB.
;    * Modified flag.  F1 and F7 only ask questions when there are
;      unsaved changes.  A "*" on the prompt line shows the state.
;    * Live window resizing.
;    * Unicode.  Text is treated as UTF-8 and shown with the ...W
;      console functions.  Invalid bytes are shown as Latin-1.
;    * Find (Ctrl-F or Alt-F, Ctrl-L repeats) and go to line (Ctrl-G).
;    * Columns are 16-bit on screen and 32-bit within a line, so long
;      lines and wide windows are no longer limited to 255 columns.
;
;  Earlier port changes (1.2): Win32 console/file APIs instead of BIOS
;  and DOS calls, F3 = COPY (also Ctrl-C) instead of PRINT, LF-only
;  files supported, file limit raised from 64 KB to FILE_MAX.
;
;  Build (x64 Native Tools Command Prompt):
;     ml64 TED64.ASM /link /subsystem:console /entry:main
;
;  Visual Studio:
;     Empty C++ project -> Build Dependencies -> Build Customizations ->
;     tick "masm".  Add TED64.ASM, platform x64, Linker > System >
;     SubSystem = Console.  (Optionally Linker > Advanced > Entry
;     Point = main to skip the C runtime.)
;
;  Usage:  TED64 [/T:n] [filename]
;
;  Conventions: RSI/RDI point into the text, RCX counts, EDX holds a
;  screen row (high word) and column (low word), CF=1 reports failure,
;  and R15 is the base of the text buffer.  Every procedure is entered
;  only at its start, sets up an RBP frame (with unwind data), and
;  leaves by "lea rsp,[rbp] / pop rbp" then RET or a JMP to another
;  procedure.  Win64 API calls go through CALLWIN.
;----------------------------------------------------------------------
        OPTION  CASEMAP:NONE
        OPTION  PROC:PRIVATE

includelib kernel32.lib
includelib user32.lib

EXTERN  GetStdHandle:PROC
EXTERN  GetConsoleMode:PROC
EXTERN  SetConsoleMode:PROC
EXTERN  SetConsoleCtrlHandler:PROC
EXTERN  SetErrorMode:PROC
EXTERN  GetConsoleScreenBufferInfo:PROC
EXTERN  SetConsoleCursorPosition:PROC
EXTERN  WriteConsoleOutputW:PROC
EXTERN  WriteConsoleW:PROC
EXTERN  CreateConsoleScreenBuffer:PROC
EXTERN  SetConsoleActiveScreenBuffer:PROC
EXTERN  SetConsoleScreenBufferSize:PROC
EXTERN  SetConsoleWindowInfo:PROC
EXTERN  GetLargestConsoleWindowSize:PROC
EXTERN  ReplaceFileW:PROC
EXTERN  GetFileAttributesExW:PROC
EXTERN  ReadConsoleInputW:PROC
EXTERN  PeekConsoleInputW:PROC
EXTERN  GetNumberOfConsoleInputEvents:PROC
EXTERN  WaitForSingleObject:PROC
EXTERN  GetCommandLineW:PROC
EXTERN  CreateFileW:PROC
EXTERN  ReadFile:PROC
EXTERN  WriteFile:PROC
EXTERN  CloseHandle:PROC
EXTERN  DeleteFileW:PROC
EXTERN  MoveFileW:PROC
EXTERN  GetFileAttributesW:PROC
EXTERN  GetLastError:PROC
EXTERN  GetFileSizeEx:PROC
EXTERN  CopyFileW:PROC
EXTERN  GetFileInformationByHandle:PROC
EXTERN  Sleep:PROC
EXTERN  VirtualAlloc:PROC
EXTERN  VirtualFree:PROC
EXTERN  Beep:PROC
EXTERN  ExitProcess:PROC
EXTERN  GlobalAlloc:PROC
EXTERN  GlobalLock:PROC
EXTERN  GlobalUnlock:PROC
EXTERN  GlobalFree:PROC
EXTERN  GlobalSize:PROC
EXTERN  CreateWindowExW:PROC            ;(user32)
EXTERN  DestroyWindow:PROC
EXTERN  OpenClipboard:PROC
EXTERN  CloseClipboard:PROC
EXTERN  EmptyClipboard:PROC
EXTERN  SetClipboardData:PROC
EXTERN  GetClipboardData:PROC
EXTERN  GetClipboardSequenceNumber:PROC

PUBLIC  main

;-----------------------------------------------------------------------
; Constants
;-----------------------------------------------------------------------
TED_VERSION     EQU     <"2.0 64-bit">

TAB             EQU     9
CR              EQU     13
LF              EQU     10

ADD_EOF_MARK    EQU     0               ;1 = append Ctrl-Z on save (DOS)
FILE_MAX        EQU     100000000h      ;4 GB maximum text size
SLACK           EQU     10000h          ;Committed space kept past the text
GAP_MIN         EQU     10000h          ;Smallest amount the gap grows by
RESERVE_SIZE    EQU     140040000h      ;FILE_MAX * 5/4 + 4 * SLACK (5 GB)
MIN_RESERVE     EQU     4000000h        ;Smallest reservation tried (64 MB)
IO_CHUNK        EQU     40000000h       ;1 GB per ReadFile/WriteFile call
MAX_COLS        EQU     1024            ;Widest window used
MAX_ROWS        EQU     300             ;Tallest window used
MIN_COLS        EQU     16
SCREEN_BYTES    EQU     MAX_COLS * MAX_ROWS * 4 + 10000h
FLUSH_CELLS     EQU     15000           ;Cells per WriteConsoleOutputW call
MAX_NAME        EQU     260             ;MAX_PATH
HIST_MAX        EQU     10000000h       ;Undo (and redo) history: 256 MB
REC_SIZE        EQU     40              ;History record footer: position,
                                        ; length, serial, group, type
REC_INS         EQU     1               ;Text was inserted
REC_DEL         EQU     2               ;Text was deleted (bytes kept)
HASH_MAX        EQU     10000000h       ;Files up to 256 MB are also
                                        ; compared by content
FNV_BASIS       EQU     0CBF29CE484222325h  ;64-bit FNV-1a hash
FNV_PRIME       EQU     100000001B3h
LINE_MAX        EQU     10000h          ;Bytes kept for F8/F9/F12
FIND_MAX        EQU     200             ;Longest search string (bytes)
T_END           EQU     0               ;Pattern node types: end,
T_LIT           EQU     1               ; a character (case folded),
T_ANY           EQU     2               ; any character (.),
T_CLASS         EQU     3               ; [...] and [^...],
T_NCLASS        EQU     4
T_BOL           EQU     5               ; ^ and $,
T_EOL           EQU     6
T_DIG           EQU     7               ; \d \D \w \W \s \S
T_NDIG          EQU     8
T_WRD           EQU     9
T_NWRD          EQU     10
T_SPC           EQU     11
T_NSPC          EQU     12
Q_ONE           EQU     0               ;Repeats: once, *, +, ?
Q_STAR          EQU     1
Q_PLUS          EQU     2
Q_OPT           EQU     3
STEP_LIMIT      EQU     100000000       ;Backtracking steps per search
MAX_MARGIN      EQU     7FFFFF00h       ;Furthest horizontal scroll
COMMON_LVB_LEADING_BYTE  EQU 100h       ;First cell of a wide character
COMMON_LVB_TRAILING_BYTE EQU 200h       ;Second cell

STD_INPUT_HANDLE        EQU     -10
STD_OUTPUT_HANDLE       EQU     -11
GENERIC_READ            EQU     80000000h
GENERIC_WRITE           EQU     40000000h
FILE_SHARE_READ         EQU     1
FILE_SHARE_WRITE        EQU     2
FILE_SHARE_DELETE       EQU     4
FILE_READ_ATTRIBUTES    EQU     80h
CREATE_NEW              EQU     1
TRUNCATE_EXISTING       EQU     5
CONSOLE_TEXTMODE_BUFFER EQU     1
CREATE_ALWAYS           EQU     2
CF_UNICODETEXT          EQU     13
GMEM_MOVEABLE           EQU     2
CLIP_MAX                EQU     40000000h  ;Largest text put on the clipboard
OPEN_EXISTING           EQU     3
FILE_ATTRIBUTE_ARCHIVE  EQU     20h
FILE_ATTRIBUTE_NORMAL   EQU     80h
INVALID_HANDLE_VALUE    EQU     -1
INVALID_FILE_ATTRIBUTES EQU     -1
FILE_ATTRIBUTE_READONLY EQU     1
FILE_ATTRIBUTE_DIRECTORY EQU    10h
REPLACEFILE_IGNORE_MERGE_ERRORS EQU 2
ERROR_FILE_NOT_FOUND    EQU     2
ERROR_PATH_NOT_FOUND    EQU     3
ERROR_ACCESS_DENIED     EQU     5
ERROR_SHARING_VIOLATION EQU     32
CTRL_BREAK_EVENT        EQU     1
CTRL_CLOSE_EVENT        EQU     2
CTRL_LOGOFF_EVENT       EQU     5
CTRL_SHUTDOWN_EVENT     EQU     6
INFINITE                EQU     -1
GET_FILE_EX_INFO_STANDARD EQU   0
MEM_COMMIT              EQU     1000h
MEM_RESERVE             EQU     2000h
MEM_DECOMMIT            EQU     4000h
PAGE_NOACCESS           EQU     1
PAGE_READWRITE          EQU     4
SEM_FAILCRITICALERRORS  EQU     1
ENABLE_WINDOW_INPUT     EQU     8
WAIT_TIMEOUT            EQU     102h
KEY_EVENT               EQU     1
WINDOW_BUFFER_SIZE_EVENT EQU    4
ALT_PRESSED             EQU     3       ;RIGHT_ALT or LEFT_ALT
CTRL_PRESSED            EQU     0Ch     ;RIGHT_CTRL or LEFT_CTRL
RESIZE_KEY              EQU     0FF00h  ;GET_KEY result after a resize

; INPUT_RECORD / KEY_EVENT_RECORD field offsets
IR_EVENTTYPE    EQU     0
IR_KEYDOWN      EQU     4
IR_VKEY         EQU     10
IR_SCAN         EQU     12
IR_CHAR         EQU     14
IR_CTRLSTATE    EQU     16

;-----------------------------------------------------------------------
; CALLWIN - call a Win64 API function with an aligned stack and shadow
; space.  RCX, RDX, R8, R9 must already hold arguments 1-4.  Optional
; arguments 5-7 must be registers (not RBX) or 32-bit immediates.  RBX
; holds the old stack pointer; RBP stays the caller's frame pointer.
; RAX, RCX, RDX, R8-R11 are destroyed (Win64 volatile registers).
;-----------------------------------------------------------------------
CALLWIN MACRO   fn, a5, a6, a7
        push    rbx                     ;(RBP is the frame pointer)
        mov     rbx, rsp
        and     rsp, -16
        sub     rsp, 64
  IFNB <a5>
        mov     qword ptr [rsp+32], a5
  ENDIF
  IFNB <a6>
        mov     qword ptr [rsp+40], a6
  ENDIF
  IFNB <a7>
        mov     qword ptr [rsp+48], a7
  ENDIF
        call    fn
        mov     rsp, rbx
        pop     rbx
ENDM

PUSH_VOLATILE   MACRO
        push    rax
        push    rcx
        push    rdx
        push    r8
        push    r9
        push    r10
        push    r11
ENDM

POP_VOLATILE    MACRO
        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     rdx
        pop     rcx
        pop     rax
ENDM

;-----------------------------------------------------------------------
; Text positions are pointers into the file buffer that never point
; inside the gap: the position right at the gap is always written as
; GAP_END.  NEXTP and PREVP step such a pointer one byte, jumping over
; the gap.  Only the flags are changed.
;-----------------------------------------------------------------------
NEXTP   MACRO   reg
        LOCAL   np_ok
        inc     reg
        cmp     reg, GAP_START
        jne     np_ok
        mov     reg, GAP_END
np_ok:
ENDM

PREVP   MACRO   reg
        LOCAL   pp_ok
        cmp     reg, GAP_END
        jne     pp_ok
        mov     reg, GAP_START
pp_ok:
        dec     reg
ENDM

;-----------------------------------------------------------------------
; Local data area
;-----------------------------------------------------------------------
.data

COPYRIGHT       DB      CR,LF,"TED ",TED_VERSION," (c) 1988 Ziff Communications Co."
                DB      CR,LF,"PC Magazine ",0E2h,96h,0A0h," Tom Kihlken",CR,LF,0

FILE_TOO_BIG    DB      "File too big (the limit is 4 GB)",0
READ_ERR_MESS   DB      "Read error",0
MEMORY_ERROR    DB      "Not enough memory",0
NO_CONSOLE_MESS DB      "TED requires a console window",0
IN_USE_MESS     DB      "Can't open - the file is in use by another program",0
ACCESS_MESS     DB      "Can't open - access denied",0
FOLDER_MESS     DB      "Can't open - that is a folder, not a file",0
OPEN_ERR_MESS   DB      "Can't open the file",0
NAME_SEP        DB      ": ",0
USAGE_MESS      DB      "Usage: TED64 [/T:n] [filename]   (n = tab width, 1-32)",0

PROMPT_STRING   DB      "1ABORT",0,"2UNDO",0,"3COPY",0
                DB      "4MARK",0,"5CUT",0,"6PASTE",0,"7EXIT",0
                DB      "8DEL EOL",0,"9DEL L",0,"10SAVE",0,"12UDEL L",0,0
PROMPT_LENGTH   =       $ - PROMPT_STRING
PROMPT_KEYS     =       11              ;Number of keys in the prompt
PROMPT_WIDE     =       PROMPT_LENGTH - 1 + PROMPT_KEYS  ;Width with spaces

VERIFY_MESS     DB      "Lose Changes (Y/N)?",0
SAVE_MESS       DB      "Save as: ",0
FIND_MESS       DB      "Find: ",0
FIND_RE_MESS    DB      "Find (regex): ",0
REPL_MESS       DB      "Replace: ",0
REPL_RE_MESS    DB      "Replace (regex): ",0
WITH_MESS       DB      "With: ",0
ASK_MESS        DB      "Replace? (Y)es (N)o (A)ll  Esc stops",0
REPLACED_MESS   DB      " replaced",0
BAD_RE_MESS     DB      "Bad regular expression",0
COMPLEX_MESS    DB      "Search too complex - stopped",0
        ALIGN   8
FIND_PROMPTS    DQ      FIND_MESS, FIND_RE_MESS
REPL_PROMPTS    DQ      REPL_MESS, REPL_RE_MESS
FIND_REGEX      DB      0               ;1 = Find/Replace use regex
PAT_FIRST       DB      0               ;ASCII letter every match starts
SEARCH_ABORT    DB      0               ; with (0 if none); 1 = gave up
        ALIGN   4
; Code points that count as letters or digits for \w (besides ASCII)
WORD_TABLE      DD      000AAH, 000AAH, 000B5H, 000B5H, 000BAH, 000BAH
                DD      000C0H, 000D6H, 000D8H, 000F6H, 000F8H, 002FFH
                DD      00370H, 00373H, 00376H, 0037DH, 00386H, 00386H
                DD      00388H, 003FFH, 00400H, 00481H, 0048AH, 0052FH
                DD      00531H, 00587H, 005D0H, 005EAH, 00620H, 0064AH
                DD      00660H, 00669H, 03041H, 03096H, 030A1H, 030FAH
                DD      03400H, 04DBFH, 04E00H, 09FFFH, 0AC00H, 0D7A3H
WORD_COUNT      EQU     ($ - WORD_TABLE) / 8
; Space characters for \s (besides 9-0Dh, 1Ch-1Fh and 20h)
SPACE_TABLE     DD      00085H, 00085H, 000A0H, 000A0H, 01680H, 01680H
                DD      02000H, 0200AH, 02028H, 02029H, 0202FH, 0202FH
                DD      0205FH, 0205FH, 03000H, 03000H
SPACE_COUNT     EQU     ($ - SPACE_TABLE) / 8
LINE_MESS       DB      "Go to line: ",0
NOT_FOUND_MESS  DB      "Not found",0
READ_ONLY_MESS  DB      "File is read-only - choose another name",0
BAK_RO_MESS     DB      "The .BAK file is read-only - choose another name",0
BAK_STUCK_MESS  DB      "Can't replace the .BAK file - choose another name",0
CHANGED_MESS    DB      "File changed on disk by another program - overwrite? (Y/N)",0
NO_PATH_MESS    DB      "That folder doesn't exist - choose another name",0
FOLDER_SAVE_MESS DB     "That is a folder - choose another name",0
CANT_CREATE_MESS DB     "Can't create the file - choose another name",0
NO_MEM_SAVE_MESS DB     "Not enough memory to save",0
BAK_COPY_MESS   DB      "Can't make the .BAK copy - choose another name",0
KEPT_MESS       DB      "Couldn't save it - your text is in ",0
SAVED_MESS      DB      "Saved",0
RECOVER_MESS    DB      "Unsaved text from a window that was closed is in ",0
SAV_EXT         DB      ".SAV",0
STATIC_CLASS    DW      "S","T","A","T","I","C",0
        ALIGN   8
CLIP_HWND       DQ      0               ;Window that owns the clipboard
CLIP_SEQ        DD      0               ;Clipboard number when TED last
                                        ; put or took text there
PREV_CR         DB      0               ;1 if the last key was Enter
DEFAULT_SAV     DB      "TED.SAV",0
        ALIGN   8
BUF_BASE        DQ      0               ;Same as R15, for other threads
BUSY            DD      1               ;0 while waiting for a key
HANDLER_ACTIVE  DD      0               ;1 once the window is closing
SAV_COUNT       DD      0
DOT_TMP         DB      ".$$$",0
DOT_BAK         DB      ".BAK",0

; Code page 437 pictures for the control characters 00h-1Fh
GLYPH_TABLE     DW      0020h,263Ah,263Bh,2665h,2666h,2663h,2660h,2022h
                DW      25D8h,25CBh,25D9h,2642h,2640h,266Ah,266Bh,263Ch
                DW      25BAh,25C4h,2195h,203Ch,00B6h,00A7h,25ACh,21A8h
                DW      2191h,2193h,2192h,2190h,221Fh,2194h,25B2h,25BCh

DIRTY_BITS      DB      1
NORMAL          DB      07H
INVERSE         DB      70H
        ALIGN   4
; Ranges of code points shown two cells wide (East Asian Wide and
; Fullwidth, and the emoji blocks), in increasing order
WIDE_TABLE      DD      01100H, 0115FH, 02E80H, 0303EH, 03041H, 033FFH
                DD      03400H, 04DBFH, 04E00H, 09FFFH, 0A000H, 0A4CFH
                DD      0A960H, 0A97FH, 0AC00H, 0D7A3H, 0F900H, 0FAFFH
                DD      0FE10H, 0FE19H, 0FE30H, 0FE6FH, 0FF00H, 0FF60H
                DD      0FFE0H, 0FFE6H, 1F300H, 1F64FH, 1F680H, 1F6FFH
                DD      1F900H, 1F9FFH, 1FA70H, 1FAFFH, 20000H, 2FFFDH
                DD      30000H, 3FFFDH
WIDE_COUNT      EQU     ($ - WIDE_TABLE) / 8
INSERT_MODE     DB      -1
MARK_MODE       DB      0
LINE_FLAG       DB      0
SCREEN_READY    DB      0
MODE_SAVED      DB      0
OWN_SCREEN      DB      0               ;1 = drawing on our own screen buffer
LF_MODE         DB      0               ;1 = file uses LF-only lines
MODIFIED        DB      0               ;1 = unsaved changes
PROMPT_DIRTY    DB      0               ;1 = prompt line needs redrawing
PROMPT_MSG      DB      0               ;1 = a message is on the prompt line
RESIZED         DB      0               ;1 = window size changed
STAMP_VALID     DB      0               ;1 = STAMP holds a file's details

                ALIGN   8
; Every stored text position is listed here, so they can be adjusted
; when the gap moves.
PTR_VARS        LABEL   QWORD
CURSOR          DQ      0               ;Character under the cursor
TOP_OF_SCREEN   DQ      0               ;First character on row 0
MARK_START      DQ      -1              ;Marked range (-1 = none)
MARK_END        DQ      0
MARK_HOME       DQ      0               ;Where marking started
NUM_PTRS        =       ($ - PTR_VARS) / 8

GAP_START       DQ      0               ;First byte of the gap
GAP_END         DQ      0               ;First byte after the gap
LAST_CHAR       DQ      0               ;One past the end of the text
TEXT_BEG        DQ      0               ;Position of the first character
NAME_POINTER    DQ      0               ;Start of filename
LINE_LENGTH     DQ      0
PASTE_SEG       DQ      0               ;Paste buffer base
PASTE_SIZE      DQ      0
CONV_BUF        DQ      0               ;Line-ending conversion buffer
SCREEN_BUF      DQ      0               ;CHAR_INFO screen image
LINE_BUFFER     DQ      0               ;Text deleted by F8/F9
PAGE_PROC       DQ      0
H_IN            DQ      0
H_OUT           DQ      0               ;Where the editor draws
H_ORIG          DQ      0               ;The console's own screen buffer
H_FILE          DQ      0
FILE_SIZE       DQ      0
; Each large buffer has a descriptor: bytes committed, bytes reserved
EDIT_COMMIT     DQ      0, 0
PASTE_COMMIT    DQ      0, 0
CONV_COMMIT     DQ      0, 0
; Undo and redo stacks: base, bytes used, then a buffer descriptor
HIST_UNDO       DQ      0, 0, 0, 0
HIST_REDO       DQ      0, 0, 0, 0
NEXT_SERIAL     DQ      0               ;Last serial number given out
EMPTY_ID        DQ      0               ;State number of an empty history
SAVED_SERIAL    DQ      0               ;State number when last saved
CUR_GROUP       DQ      0               ;Group of the current command
LOG_SERIAL      DQ      0               ;Serial and group carried over
LOG_GROUP       DQ      0               ; while undoing or redoing
UR_GROUP        DQ      0
UR_SIZE         DQ      0
FOOT_TMP        DQ      5 DUP(0)
LOG_MODE        DB      0               ;0 edit, 1 undoing, 2 redoing
START_GROUP     DB      1               ;1 = next change starts a group
LAST_CLASS      DB      0               ;Kind of the last key (CLASSIFY)
EF_PROMPT       DQ      0               ;EDIT_FIELD parameters
EF_BUF          DQ      0
EF_MAX          DQ      0

EXIT_CODE       DD      0               ;0 = normal, 1 = error
TAB_WIDTH       DD      8               ;Columns per tab stop (/T:n)
OLD_IN_MODE     DD      0
IO_COUNT        DD      0
NUM_EVENTS      DD      0
NUM_EVENTS2     DD      0
CUR_POSN        DD      0               ;Row in high word, column in low
SAVE_COLUMN     DD      0               ;Column to aim for going up/down
LEFT_MARGIN     DD      0               ;Horizontal scroll (multiple of 8)
MARGIN_COUNT    DD      0               ;Columns skipped left of margin
LINE_COL        DD      0               ;Cursor column within its line
CUR_WIDTH       DD      1               ;Width of the character there
HSCROLL_OFF     DB      0               ;1 after Ctrl+Left/Right: the
                                        ; view stays put this once
KEY_CHAR        DD      0               ;Unicode character from GET_KEY
HIGH_SURR       DD      0               ;Pending UTF-16 high surrogate
EF_UNI          DD      0
BAND_ROWS       DD      0
BAND_N          DD      0

COLUMNS         DW      80              ;Window width
SCREEN_ROWS     DW      25              ;Window height (with prompt)
ROWS            DW      23              ;Last text row
SAVE_ROW        DW      0
WIN_LEFT        DW      0
WIN_TOP         DW      0
WRITE_RECT      DW      4 DUP(0)        ;SMALL_RECT

                ALIGN   8
CSBI            DB      24 DUP(0)       ;CONSOLE_SCREEN_BUFFER_INFO
INPUT_REC       DB      24 DUP(0)       ;INPUT_RECORD
KEY_BYTES       DB      8 DUP(0)        ;UTF-8 bytes of a typed key
                ALIGN   8
; Details of the file as loaded or last saved, to spot changes made by
; other programs: which file it is, when it was written, its size, and
; (for files up to HASH_MAX) a hash of its contents
STAMP_INDEX     DQ      0               ;File ID
STAMP_TIME      DQ      0               ;ftLastWriteTime
STAMP_SIZE      DQ      0
STAMP_HASH      DQ      0
STAMP_VOL       DD      0               ;Volume serial number
STAMP_HASHED    DB      0               ;1 = STAMP_HASH is valid
FILE_EXISTS     DB      0               ;Save: the file is already there
                ALIGN   8
FILE_LINKS      DD      0               ;Save: number of hard links
                ALIGN   8
OUT_PTR         DQ      0               ;The bytes to write when saving
OUT_SIZE        DQ      0
OUT_HASH        DQ      0
OUT_HASHED      DB      0
                ALIGN   8
HASH_TMP        DQ      0
                ALIGN   8
BHFI            DB      56 DUP(0)       ;BY_HANDLE_FILE_INFORMATION

.data?
                ALIGN   16
NAME_BUF        DB      MAX_NAME + 16 DUP(?)
NAME_TMP        DB      MAX_NAME + 16 DUP(?)
NAME_BAK        DB      MAX_NAME + 16 DUP(?)
MSG_BUF         DB      MAX_NAME * 2 + 80 DUP(?)  ;Messages built at run time
ARG_BUF         DB      MAX_NAME * 3 + 16 DUP(?) ;One command-line argument
WNAME           DW      MAX_NAME + 16 DUP(?)  ;UTF-16 copies of the names
WTMP            DW      MAX_NAME + 16 DUP(?)  ; for the ...W file functions
WBAK            DW      MAX_NAME + 16 DUP(?)
WSAV            DW      MAX_NAME + 16 DUP(?)
STAMP_NAME      DB      MAX_NAME + 16 DUP(?)  ;Name the file stamp is for
SAV_NAME        DB      MAX_NAME + 16 DUP(?)
FIND_BUF        DB      FIND_MAX + 16 DUP(?)
REPL_BUF        DB      FIND_MAX + 16 DUP(?)
                ALIGN   16
PAT_NODES       DD      (FIND_MAX + 2) * 4 DUP(?)  ;Compiled search pattern
CLASS_ITEMS     DD      (FIND_MAX + 2) * 2 DUP(?)  ;Ranges for [...]
C_PREV          DQ      ?               ;Last node a * + ? may follow
MATCH_START     DQ      ?
STEPS           DQ      ?               ;Backtracking allowed
RP_START        DQ      ?               ;Where Replace looks next
RP_COUNT        DQ      ?
RP_ALL          DB      ?
RP_SEEN         DB      ?
NUM_BUF         DB      32 DUP(?)
WIDE_BUF        DW      520 DUP(?)

;=======================================================================
.code

;-----------------------------------------------------------------------
; We start by initializing the console, then reserve memory for the
; file and paste buffers.  Parse the command line for a filename, if one
; was input, read in the file.
;-----------------------------------------------------------------------
main            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cld
        mov     ecx, STD_INPUT_HANDLE
        CALLWIN GetStdHandle
        mov     H_IN, rax
        mov     ecx, STD_OUTPUT_HANDLE
        CALLWIN GetStdHandle
        mov     H_OUT, rax

        mov     rcx, H_OUT
        lea     rdx, CSBI
        CALLWIN GetConsoleScreenBufferInfo
        test    eax, eax
        jz      NOT_A_CONSOLE
        mov     rcx, H_IN
        lea     rdx, OLD_IN_MODE
        CALLWIN GetConsoleMode
        test    eax, eax
        jz      NOT_A_CONSOLE
        mov     MODE_SAVED, 1

        call    SET_GEOMETRY            ;Window size and position
        mov     al, byte ptr CSBI+8     ;Current attribute
        mov     NORMAL, al              ;Save the normal attribute
        xor     al, 1110111B            ;Flip the color bits
        mov     INVERSE, al

; Raw keyboard input plus window-size events; Ctrl-C is just a key
        mov     rcx, H_IN
        mov     edx, ENABLE_WINDOW_INPUT
        CALLWIN SetConsoleMode
        xor     ecx, ecx
        mov     edx, 1
        CALLWIN SetConsoleCtrlHandler   ;Ignore Ctrl-C
        lea     rcx, CTRL_HANDLER
        mov     edx, 1
        CALLWIN SetConsoleCtrlHandler   ;And Ctrl-Break (see CTRL_HANDLER)
        mov     ecx, SEM_FAILCRITICALERRORS
        CALLWIN SetErrorMode            ;Replaces the INT 24 handler

; Reserve address space for the file, paste and conversion buffers.
; Memory is committed only as each buffer grows (see COMMIT_BUF).
        mov     rdx, RESERVE_SIZE
        lea     r8, EDIT_COMMIT
        call    RESERVE_BUF
        jz      NOT_ENOUGH
        mov     r15, rax                ;R15 = file buffer base
        mov     BUF_BASE, rax           ; (also for the control handler)
        mov     rdx, RESERVE_SIZE
        lea     r8, PASTE_COMMIT
        call    RESERVE_BUF
        jz      NOT_ENOUGH
        mov     PASTE_SEG, rax          ;Use this for the paste buffer
        mov     rdx, RESERVE_SIZE
        lea     r8, CONV_COMMIT
        call    RESERVE_BUF
        jz      NOT_ENOUGH
        mov     CONV_BUF, rax           ;Buffer for line-ending changes
        mov     edx, HIST_MAX           ;Undo and redo history (if the
        lea     r8, HIST_UNDO+16        ; space can't be had, editing
        call    RESERVE_BUF             ; works without it)
        jz      NO_HISTORY
        mov     HIST_UNDO, rax
        mov     edx, HIST_MAX
        lea     r8, HIST_REDO+16
        call    RESERVE_BUF
        mov     HIST_REDO, rax
        jnz     NO_HISTORY
        mov     HIST_UNDO, rax          ;Need both, or neither
NO_HISTORY:
        mov     rcx, r15
        xor     edx, edx
        lea     r8, EDIT_COMMIT
        call    COMMIT_BUF              ;Commit a first small piece
        jc      NOT_ENOUGH
        mov     rcx, PASTE_SEG
        xor     edx, edx
        lea     r8, PASTE_COMMIT
        call    COMMIT_BUF
        jc      NOT_ENOUGH
        xor     ecx, ecx                ;Screen image and line buffer
        mov     edx, SCREEN_BYTES + LINE_MAX + 1000h
        mov     r8d, MEM_COMMIT or MEM_RESERVE
        mov     r9d, PAGE_READWRITE
        CALLWIN VirtualAlloc
        test    rax, rax
        jnz     GOT_ENOUGH
NOT_ENOUGH:
        lea     rsi, MEMORY_ERROR
        jmp     ERR_EXIT
NOT_A_CONSOLE:
        lea     rsi, NO_CONSOLE_MESS
ERR_EXIT:
        call    PRINT_STR               ;Write the error message
        mov     EXIT_CODE, 1            ;Tell batch files it failed
        lea     rsp, [rbp]
        pop     rbp
        jmp     QUIT                    ;Exit this program
GOT_ENOUGH:
        mov     SCREEN_BUF, rax
        add     rax, SCREEN_BYTES
        mov     LINE_BUFFER, rax
        mov     CURSOR, r15             ;Empty text, empty gap
        mov     TOP_OF_SCREEN, r15
        mov     MARK_END, r15
        mov     MARK_HOME, r15
        mov     GAP_START, r15
        mov     GAP_END, r15
        mov     LAST_CHAR, r15
        mov     TEXT_BEG, r15

;-----------------------------------------------------------------------
; Get the filename from the command line.  Skip the program name, then
; take the first (optionally quoted) argument.
;-----------------------------------------------------------------------
GET_FILENAME:
        lea     rdi, NAME_BUF
        mov     NAME_POINTER, rdi       ;No filename yet
        mov     byte ptr [rdi], 0
        CALLWIN GetCommandLineW         ;UTF-16, so any name works
        mov     rsi, rax
        call    NEXT_ARG                ;Skip the program name
ARG_LOOP:
        call    NEXT_ARG                ;Next argument, as UTF-8
        jc      ARGS_DONE
        mov     al, ARG_BUF
        cmp     al, "/"                 ;An option?
        je      AN_OPTION
        cmp     al, "-"
        je      AN_OPTION
        cmp     byte ptr NAME_BUF, 0    ;The first other argument is
        jne     ARG_LOOP                ; the filename
        push    rsi
        lea     rsi, ARG_BUF
        lea     rdi, NAME_BUF
        mov     ecx, MAX_NAME - 6       ;Room for the longest extension
COPY_NAME:
        lodsb
        test    al, al
        jz      NAME_COPIED
        stosb
        dec     ecx
        jnz     COPY_NAME
NAME_TRIM:
        mov     al, [rdi-1]             ;Too long: don't stop in the
        and     al, 0C0H                ; middle of a UTF-8 character
        cmp     al, 80H
        jne     NAME_COPIED
        dec     rdi
        jmp     NAME_TRIM
NAME_COPIED:
        mov     byte ptr [rdi], 0
        pop     rsi
        jmp     ARG_LOOP
AN_OPTION:
        mov     al, ARG_BUF+1           ;Only /T:n is known
        or      al, 20H
        cmp     al, "t"
        jne     BAD_OPTION
        lea     rdi, ARG_BUF+2
        cmp     byte ptr [rdi], ":"
        jne     OPT_NUMBER
        inc     rdi
OPT_NUMBER:
        xor     ecx, ecx
        xor     edx, edx                ;Digits seen
OPT_DIGIT:
        movzx   eax, byte ptr [rdi]
        test    eax, eax
        jz      OPT_END
        sub     eax, "0"
        cmp     eax, 9
        ja      BAD_OPTION
        imul    ecx, ecx, 10
        add     ecx, eax
        cmp     ecx, 1000
        ja      BAD_OPTION
        inc     edx
        inc     rdi
        jmp     OPT_DIGIT
OPT_END:
        test    edx, edx
        jz      BAD_OPTION
        cmp     ecx, 1
        jb      BAD_OPTION
        cmp     ecx, 32
        ja      BAD_OPTION
        mov     TAB_WIDTH, ecx
        jmp     ARG_LOOP
BAD_OPTION:
        lea     rsi, USAGE_MESS
        jmp     ERR_EXIT
ARGS_DONE:
        cmp     byte ptr NAME_BUF, 0    ;Any filename?
        je      NO_FILENAME

        mov     rsi, NAME_POINTER
        lea     rdi, WNAME
        call    TO_WIDE                 ;UTF-16 for CreateFileW
        lea     rcx, WNAME              ;Setup to open file.  Let other
        mov     edx, GENERIC_READ       ; programs keep reading and
        mov     r8d, FILE_SHARE_READ or FILE_SHARE_WRITE  ; writing it
        xor     r9d, r9d
        CALLWIN CreateFileW, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0
        cmp     rax, INVALID_HANDLE_VALUE
        jne     FILE_OPENED
        CALLWIN GetLastError            ;Only a missing file is a new
        cmp     eax, ERROR_FILE_NOT_FOUND  ; file.  Anything else would
        je      NO_FILENAME             ; give an empty buffer that
        cmp     eax, ERROR_PATH_NOT_FOUND  ; could be saved over the
        je      NO_FILENAME             ; real file, so stop instead
        lea     rsi, IN_USE_MESS
        cmp     eax, ERROR_SHARING_VIOLATION
        je      OPEN_FAILED
        lea     rsi, OPEN_ERR_MESS
        cmp     eax, ERROR_ACCESS_DENIED
        jne     OPEN_FAILED
        lea     rsi, ACCESS_MESS        ;Access denied: a folder gives
        lea     rcx, WNAME              ; this too
        CALLWIN GetFileAttributesW
        cmp     eax, INVALID_FILE_ATTRIBUTES
        je      OPEN_FAILED
        test    eax, FILE_ATTRIBUTE_DIRECTORY
        jz      OPEN_FAILED
        lea     rsi, FOLDER_MESS
OPEN_FAILED:
        call    PRINT_STR               ;"Can't open ...: name"
        lea     rsi, NAME_SEP
        call    PRINT_STR
        mov     rsi, NAME_POINTER
        jmp     ERR_EXIT
FILE_OPENED:
        mov     H_FILE, rax
        mov     rcx, rax
        lea     rdx, FILE_SIZE
        CALLWIN GetFileSizeEx           ;How big is the file?
        test    eax, eax
        jz      READ_ERROR
        mov     rax, FILE_MAX
        cmp     FILE_SIZE, rax          ;Too big to edit?
        jae     TOO_BIG
        mov     rcx, CONV_BUF
        mov     rdx, FILE_SIZE
        lea     r8, CONV_COMMIT
        call    COMMIT_BUF              ;Get memory to read it into
        jc      NOT_ENOUGH
        mov     r12, CONV_BUF           ;Where to read
        mov     r13, FILE_SIZE          ;Bytes left to read
READ_LOOP:
        test    r13, r13                ;All read?
        jz      READ_DONE
        mov     r8, r13
        mov     eax, IO_CHUNK
        cmp     r8, rax                 ;Read at most 1 GB at a time
        jbe     READ_CHUNK
        mov     r8, rax
READ_CHUNK:
        mov     rcx, H_FILE
        mov     rdx, r12
        lea     r9, IO_COUNT
        CALLWIN ReadFile, 0
        test    eax, eax
        jz      READ_ERROR
        mov     eax, IO_COUNT
        test    eax, eax                ;End of file reached early?
        jz      READ_DONE
        add     r12, rax
        sub     r13, rax
        jmp     READ_LOOP
READ_ERROR:
        lea     rsi, READ_ERR_MESS
        jmp     ERR_EXIT
TOO_BIG:
        lea     rsi, FILE_TOO_BIG
        jmp     ERR_EXIT
READ_DONE:
        sub     r12, CONV_BUF           ;R12 = number of bytes read
        call    RECORD_LOADED           ;Note the file's details
        mov     rcx, H_FILE
        CALLWIN CloseHandle             ;Close the file

; Pass 1: count the bare LFs, since each one needs a CR added.
        mov     rsi, CONV_BUF
        mov     rcx, r12
        xor     r9d, r9d                ;Count of bare LFs
        xor     edx, edx                ;DL = previous character
        test    rcx, rcx
        jz      COUNT_DONE
COUNT_LOOP:
        lodsb
        cmp     al, LF                  ;Line feed?
        jne     COUNT_NEXT
        cmp     dl, CR                  ;Preceded by a CR?
        je      COUNT_NEXT
        inc     r9
COUNT_NEXT:
        mov     dl, al
        dec     rcx
        jnz     COUNT_LOOP
COUNT_DONE:
        lea     rdx, [r12+r9]           ;Size once converted
        mov     rax, FILE_MAX
        cmp     rdx, rax
        ja      TOO_BIG
        mov     rcx, r15
        lea     r8, EDIT_COMMIT
        call    COMMIT_BUF              ;Get memory for the edit buffer
        jc      NOT_ENOUGH

; Pass 2: copy the file into the edit buffer, turning bare LFs into
; CR/LF.  Count both kinds of line ending to decide how to save it.
        mov     rsi, CONV_BUF
        mov     rcx, r12                ;Number of bytes read in
        mov     rdi, r15                ;Edit buffer
        xor     r9d, r9d                ;Count of bare LFs
        xor     r10d, r10d              ;Count of CR/LF pairs
        xor     edx, edx                ;DL = previous character
        test    rcx, rcx
        jz      CONV_DONE
CONV_LOOP:
        lodsb
        cmp     al, LF                  ;Line feed?
        jne     CONV_STORE
        cmp     dl, CR                  ;Already preceded by a CR?
        je      CONV_CRLF
        inc     r9                      ;Bare LF - add a CR first
        mov     byte ptr [rdi], CR
        inc     rdi
        jmp     CONV_STORE
CONV_CRLF:
        inc     r10
CONV_STORE:
        stosb
        mov     dl, al
        dec     rcx
        jnz     CONV_LOOP
CONV_DONE:
        cmp     r9, r10                 ;Mostly LF-only lines?
        jbe     FREE_CONV
        mov     LF_MODE, 1              ;Save it back the same way
FREE_CONV:
        mov     rcx, CONV_BUF           ;Give back the read buffer
        xor     edx, edx
        mov     r8d, MEM_DECOMMIT
        CALLWIN VirtualFree
        mov     CONV_COMMIT, 0
LOOK_FOR_EOF:
        cmp     rdi, r15                ;Empty file?
        je      NO_EOF
        cmp     byte ptr [rdi-1], 1AH   ;Was last char an EOF mark?
        jne     NO_EOF                  ;If not, everything is OK
        dec     rdi                     ;Backup to last character
NO_EOF:
        mov     LAST_CHAR, rdi          ;Save the file size
        mov     GAP_START, rdi          ;The gap starts out empty,
        mov     GAP_END, rdi            ; at the end of the text
NO_FILENAME:
        call    USE_OWN_SCREEN          ;Keep the user's screen intact
        call    REDO_PROMPT             ;Draw the prompt line
        call    MAKE_SAV_NAME           ;Text kept from a session that
        lea     rcx, WSAV               ; was closed without saving?
        CALLWIN GetFileAttributesW
        cmp     eax, INVALID_FILE_ATTRIBUTES
        je      NO_RECOVERY
        lea     rdi, MSG_BUF
        lea     rsi, RECOVER_MESS
        call    STRCPY_END
        lea     rsi, SAV_NAME
        call    STRCPY_END
        lea     rsi, MSG_BUF
        call    SHOW_MESSAGE
NO_RECOVERY:
        mov     SCREEN_READY, 1

;-----------------------------------------------------------------------
; Here's the main loop.  It updates the screen, then reads a keystroke.
;-----------------------------------------------------------------------
READ_A_KEY:
        cmp     RESIZED, 0              ;Did the window change size?
        je      NOT_RESIZED
        mov     RESIZED, 0
        call    APPLY_RESIZE
NOT_RESIZED:
        cmp     PROMPT_DIRTY, 0         ;Prompt line need redrawing?
        je      PROMPT_OK
        call    REDO_PROMPT
PROMPT_OK:
        cmp     MARK_MODE, 0            ;Is the mark state on?
        je      MARK_OFF                ;If not, skip this
        or      DIRTY_BITS, 4           ;Refresh the current row
        mov     eax, CUR_POSN
        shr     eax, 16
        cmp     SAVE_ROW, ax            ;Are we on the save row?
        je      SAME_ROW                ;If yes, then redo the row only
        mov     DIRTY_BITS, 1           ;Refresh the whole screen
SAME_ROW:
        mov     rax, CURSOR             ;Get cursor location
        mov     rbx, MARK_HOME          ;Get the anchor mark position
        cmp     rax, rbx                ;Moving backward in file?
        jae     S1
        mov     MARK_START, rax         ;Switch start and end position
        mov     MARK_END, rbx
        jmp     MARK_OFF
S1:
        mov     MARK_END, rax           ;Store start and end marks
        mov     MARK_START, rbx
MARK_OFF:
        mov     edx, CUR_POSN
        mov     eax, edx
        shr     eax, 16
        mov     SAVE_ROW, ax
        call    SET_CURSOR              ;Position the cursor
        test    DIRTY_BITS, 1           ;Look at screen dirty bit
        jz      SCREEN_OK               ;If zero, screen is OK

        call    KEY_READY               ;Any keys ready?
        test    eax, eax
        jnz     CURRENT_OK              ;If yes, skip the update
        call    DISPLAY_SCREEN          ;Redraw the screen
        mov     DIRTY_BITS, 0           ;Mark screen as OK
SCREEN_OK:
        test    DIRTY_BITS, 4           ;Is the current line dirty?
        jz      CURRENT_OK              ;If not, take jump
        call    DISPLAY_CURRENT         ;Redraw the current line
        mov     DIRTY_BITS, 0           ;Mark screen as OK
CURRENT_OK:
        call    GET_KEY                 ;Read the next key
        mov     HSCROLL_OFF, 0          ;The view follows the cursor
        cmp     PROMPT_MSG, 0           ;Clear any message on the
        je      NO_MESSAGE              ; prompt line
        mov     PROMPT_MSG, 0
        push    rax
        call    REDO_PROMPT
        pop     rax
NO_MESSAGE:
        call    CLASSIFY                ;Typing, erasing or other
        cmp     al, LF                  ;An LF straight after Enter
        jne     NOT_LF                  ; (text pasted by the console
        cmp     PREV_CR, 0              ; may have both) is dropped
        je      NOT_LF
        mov     PREV_CR, 0
        jmp     READ_A_KEY
NOT_LF:
        mov     PREV_CR, 0
        cmp     al, CR
        jne     NOT_CR
        cmp     KEY_CHAR, 80H
        jae     NOT_CR
        mov     PREV_CR, 1
NOT_CR:
        cmp     KEY_CHAR, 80H           ;Non-ASCII character?
        jae     WIDE_KEY
        or      al, al                  ;Is this an extended code?
        jz      EXTENDED_CODE
        cmp     ah, 0EH                 ;Was it the backspace key?
        je      BACK_SPACE
        cmp     al, 3                   ;Ctrl-C = COPY
        je      CTRL_C
        cmp     al, 24                  ;Ctrl-X = CUT
        je      CTRL_X
        cmp     al, 22                  ;Ctrl-V = PASTE
        je      CTRL_V
        cmp     al, 17                  ;Ctrl-Q = EXIT
        je      CTRL_Q
        cmp     al, 6                   ;Ctrl-F = FIND
        je      CTRL_F
        cmp     al, 7                   ;Ctrl-G = GO TO LINE
        je      CTRL_G
        cmp     al, 12                  ;Ctrl-L = FIND AGAIN
        je      CTRL_L
        cmp     al, 8                   ;Ctrl-H = REPLACE
        je      CTRL_H
        cmp     al, 26                  ;Ctrl-Z = UNDO
        je      CTRL_Z
        cmp     al, 25                  ;Ctrl-Y = REDO
        je      CTRL_Y
        call    INSERT_KEY              ;Put this character in the file
        jmp     READ_A_KEY              ;Get another key
WIDE_KEY:
        mov     eax, KEY_CHAR
        call    INSERT_CHAR             ;Insert it as UTF-8
        jmp     READ_A_KEY
CTRL_C:
        call    COPY
        jmp     READ_A_KEY
CTRL_X:
        call    CUT
        jmp     READ_A_KEY
CTRL_V:
        call    PASTE
        jmp     READ_A_KEY
CTRL_Q:
        call    EXIT
        jmp     READ_A_KEY
CTRL_F:
        call    FIND_CMD
        jmp     READ_A_KEY
CTRL_G:
        call    GOTO_LINE
        jmp     READ_A_KEY
CTRL_L:
        call    FIND_AGAIN
        jmp     READ_A_KEY
CTRL_H:
        call    REPLACE_CMD
        jmp     READ_A_KEY
CTRL_Z:
        call    UNDO
        jmp     READ_A_KEY
CTRL_Y:
        call    REDO
        jmp     READ_A_KEY
CTRL_LEFT:
        call    SH_LEFT
        jmp     READ_A_KEY
CTRL_RIGHT:
        call    SH_RIGHT
        jmp     READ_A_KEY
BACK_SPACE:
        mov     rax, CURSOR
        cmp     rax, TEXT_BEG           ;At start of file?
        je      IGNORE_KEY              ;If at start, can't backspace
        call    LEFT                    ;Move left one space
        call    DEL_CHAR                ;And delete the character
        jmp     READ_A_KEY
EXTENDED_CODE:
        cmp     ah, 84H                 ;Is it control PgUp?
        jne     NOT_TOP
        call    TOP
        jmp     READ_A_KEY
NOT_TOP:
        cmp     ah, 76H                 ;Is it control PgDn?
        jne     NOT_BOTTOM
        call    BOTTOM
IGNORE_KEY:
        jmp     READ_A_KEY
NOT_BOTTOM:
        cmp     ah, 73H                 ;Is it control left arrow?
        je      CTRL_LEFT
        cmp     ah, 74H                 ;Is it control right arrow?
        je      CTRL_RIGHT
        cmp     ah, 21H                 ;Alt-F = FIND
        je      CTRL_F
        cmp     ah, 58H                 ;Skip keys above F12
        ja      IGNORE_KEY
        xchg    ah, al
        sub     al, 3BH                 ;Also skip low numbered keys
        jc      IGNORE_KEY
        movzx   ebx, al                 ;Make the code an index
        lea     rcx, DISPATCH_TABLE
        call    qword ptr [rcx+rbx*8]   ;Call the key procedure
        jmp     READ_A_KEY              ;Then read another key
main            ENDP

;-----------------------------------------------------------------------
; Console control handler.  Windows calls this on a thread of its own,
; with the event in ECX, using the normal Win64 calling convention, on
; a thread of its own.  Ctrl-C and Ctrl-Break are ignored (TRUE =
; handled), so they can't end TED and lose unsaved text.  When the
; window is closed, or the user logs off or Windows shuts down, unsaved
; text is written to the recovery file once the editor is waiting for a
; key (GET_KEY then stops it touching the text), and Windows is left to
; end the program (FALSE).
;-----------------------------------------------------------------------
CTRL_HANDLER    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     ecx, CTRL_BREAK_EVENT   ;CTRL_C_EVENT = 0, BREAK = 1
        jbe     CH_TRUE                 ;Handled - ignore it
        cmp     ecx, CTRL_CLOSE_EVENT   ;Window closed,
        je      CH_SAVE
        cmp     ecx, CTRL_LOGOFF_EVENT  ; user logging off, or
        jb      CH_FALSE
        cmp     ecx, CTRL_SHUTDOWN_EVENT ; Windows shutting down
        ja      CH_FALSE
CH_SAVE:
        push    rbx
        push    rsi
        push    rdi
        mov     eax, 1
        xchg    HANDLER_ACTIVE, eax     ;Tell the editor to stop
        test    eax, eax
        jnz     CH_DONE                 ;Already done once
        mov     ebx, 400                ;Wait (up to 4 seconds) for it
CH_WAIT:                                ; to finish the command
        cmp     BUSY, 0
        je      CH_READY
        mov     ecx, 10
        CALLWIN Sleep
        dec     ebx
        jnz     CH_WAIT
CH_READY:
        cmp     MODIFIED, 0             ;Any unsaved changes?
        je      CH_DONE
        call    WRITE_RECOVERY          ;Keep them in the .SAV file
CH_DONE:
        pop     rdi
        pop     rsi
        pop     rbx
CH_FALSE:
        xor     eax, eax                ;Now let Windows end the program
        lea     rsp, [rbp]
        pop     rbp
        ret
CH_TRUE:
        mov     eax, 1
        lea     rsp, [rbp]
        pop     rbp
        ret
CTRL_HANDLER    ENDP

;-----------------------------------------------------------------------
; Put the name of the recovery file in SAV_NAME and WSAV: the file's
; name with .SAV added, or TED.SAV if there is no name.
;-----------------------------------------------------------------------
MAKE_SAV_NAME   PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     rdi, SAV_NAME
        lea     rsi, NAME_BUF
        cmp     byte ptr [rsi], 0
        jne     MS_NAMED
        lea     rsi, DEFAULT_SAV
MS_NAMED:
        call    STRCPY_END
        cmp     byte ptr NAME_BUF, 0
        je      MS_WIDE
        lea     rsi, SAV_EXT
        call    STRCPY_END
MS_WIDE:
        lea     rsi, SAV_NAME
        lea     rdi, WSAV
        lea     rsp, [rbp]
        pop     rbp
        jmp     TO_WIDE
MAKE_SAV_NAME   ENDP

;-----------------------------------------------------------------------
; Write the text to the recovery file.  This runs on the thread Windows
; starts for the control handler, so it uses BUF_BASE, not R15.
;-----------------------------------------------------------------------
WRITE_RECOVERY  PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rbx
        push    r12
        push    r13
        call    MAKE_SAV_NAME
        lea     rcx, WSAV
        mov     edx, GENERIC_WRITE
        xor     r8d, r8d
        xor     r9d, r9d
        CALLWIN CreateFileW, CREATE_ALWAYS, FILE_ATTRIBUTE_ARCHIVE, 0
        cmp     rax, INVALID_HANDLE_VALUE
        je      WR_RET
        mov     rbx, rax
        mov     r12, BUF_BASE           ;The text before the gap
        mov     r13, GAP_START
        sub     r13, r12
        call    WR_PART
        mov     r12, GAP_END            ;And after it
        mov     r13, LAST_CHAR
        sub     r13, r12
        call    WR_PART
        mov     rcx, rbx
        CALLWIN CloseHandle
WR_RET:
        pop     r13
        pop     r12
        pop     rbx
        lea     rsp, [rbp]
        pop     rbp
        ret
WRITE_RECOVERY  ENDP

; Write R13 bytes from R12 to the file RBX
WR_PART         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
WP_LOOP:
        test    r13, r13
        jz      WP_DONE
        mov     r8, r13
        mov     eax, IO_CHUNK
        cmp     r8, rax
        jbe     WP_CHUNK
        mov     r8, rax
WP_CHUNK:
        mov     rcx, rbx
        mov     rdx, r12
        lea     r9, SAV_COUNT
        CALLWIN WriteFile, 0
        test    eax, eax
        jz      WP_DONE
        mov     eax, SAV_COUNT
        test    eax, eax
        jz      WP_DONE
        add     r12, rax
        sub     r13, rax
        jmp     WP_LOOP
WP_DONE:
        lea     rsp, [rbp]
        pop     rbp
        ret
WR_PART         ENDP

;-----------------------------------------------------------------------
; Keys with no command in DISPATCH_TABLE come here.  (The original
; jumped back to the main loop from here without returning, which left
; a return address on the stack for every such key.)
;-----------------------------------------------------------------------
BAD_KEY         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     rsp, [rbp]
        pop     rbp
        ret
BAD_KEY         ENDP

;-----------------------------------------------------------------------
; Create a screen buffer of our own and make it the active one, so the
; user's console contents come back untouched when TED quits.  It is
; made as large as the console window can get (so the window can still
; be enlarged), and the window keeps its current size.  If any of this
; fails, TED simply draws on the console's own screen buffer instead.
;-----------------------------------------------------------------------
USE_OWN_SCREEN  PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     ecx, GENERIC_READ or GENERIC_WRITE
        mov     edx, FILE_SHARE_READ or FILE_SHARE_WRITE
        xor     r8d, r8d
        mov     r9d, CONSOLE_TEXTMODE_BUFFER
        CALLWIN CreateConsoleScreenBuffer, 0
        cmp     rax, INVALID_HANDLE_VALUE
        je      UOS_RET
        test    rax, rax
        jz      UOS_RET
        mov     rbx, rax                ;RBX = the new buffer
        mov     rcx, H_OUT
        CALLWIN GetLargestConsoleWindowSize
        movzx   ecx, ax                 ;Largest window width
        shr     eax, 16                 ;Largest window height
        cmp     ecx, MAX_COLS
        jbe     UOS_W1
        mov     ecx, MAX_COLS
UOS_W1: movzx   edx, word ptr CSBI+14   ;At least the current window
        sub     dx, word ptr CSBI+10
        inc     edx
        cmp     ecx, edx
        jae     UOS_W2
        mov     ecx, edx
UOS_W2: cmp     eax, MAX_ROWS
        jbe     UOS_H1
        mov     eax, MAX_ROWS
UOS_H1: movzx   edx, word ptr CSBI+16
        sub     dx, word ptr CSBI+12
        inc     edx
        cmp     eax, edx
        jae     UOS_H2
        mov     eax, edx
UOS_H2: shl     eax, 16
        or      eax, ecx
        mov     edx, eax                ;COORD {width, height}
        mov     rcx, rbx
        CALLWIN SetConsoleScreenBufferSize
        mov     rcx, rbx
        CALLWIN SetConsoleActiveScreenBuffer
        test    eax, eax
        jz      UOS_FAIL
        movzx   eax, word ptr CSBI+14   ;Same window size as before,
        sub     ax, word ptr CSBI+10    ; at the top left
        mov     WRITE_RECT+4, ax
        movzx   eax, word ptr CSBI+16
        sub     ax, word ptr CSBI+12
        mov     WRITE_RECT+6, ax
        mov     WRITE_RECT, 0
        mov     WRITE_RECT+2, 0
        mov     rcx, rbx
        mov     edx, 1                  ;Absolute coordinates
        lea     r8, WRITE_RECT
        CALLWIN SetConsoleWindowInfo
        mov     rax, H_OUT
        mov     H_ORIG, rax
        mov     H_OUT, rbx              ;Draw on the new buffer
        mov     OWN_SCREEN, 1
        mov     rcx, rbx                ;Its window may differ a little
        lea     rdx, CSBI
        CALLWIN GetConsoleScreenBufferInfo
        test    eax, eax
        jz      UOS_RET
        call    SET_GEOMETRY
UOS_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
UOS_FAIL:
        mov     rcx, rbx                ;Couldn't switch - stay put
        CALLWIN CloseHandle
        lea     rsp, [rbp]
        pop     rbp
        ret
USE_OWN_SCREEN  ENDP

;-----------------------------------------------------------------------
; Read the window size from CSBI.  Returns CF=1 if anything changed.
;-----------------------------------------------------------------------
SET_GEOMETRY    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    r8
        push    r9
        movzx   eax, word ptr CSBI+10   ;srWindow.Left
        movzx   ecx, word ptr CSBI+14   ;srWindow.Right
        sub     ecx, eax
        inc     ecx                     ;Number of columns
        cmp     ecx, MAX_COLS
        jbe     SG_1
        mov     ecx, MAX_COLS
SG_1:   cmp     ecx, MIN_COLS
        jae     SG_2
        mov     ecx, MIN_COLS
SG_2:   movzx   edx, word ptr CSBI+12   ;srWindow.Top
        movzx   r9d, word ptr CSBI+16   ;srWindow.Bottom
        sub     r9d, edx
        inc     r9d                     ;Number of rows
        cmp     r9d, MAX_ROWS
        jbe     SG_3
        mov     r9d, MAX_ROWS
SG_3:   cmp     r9d, 3
        jae     SG_4
        mov     r9d, 3
SG_4:   xor     r8d, r8d                ;Count of changes
        cmp     ax, WIN_LEFT
        je      SG_5
        inc     r8d
        mov     WIN_LEFT, ax
SG_5:   cmp     cx, COLUMNS
        je      SG_6
        inc     r8d
        mov     COLUMNS, cx
SG_6:   cmp     dx, WIN_TOP
        je      SG_7
        inc     r8d
        mov     WIN_TOP, dx
SG_7:   cmp     r9w, SCREEN_ROWS
        je      SG_8
        inc     r8d
        mov     SCREEN_ROWS, r9w
        sub     r9d, 2                  ;Last row is for prompt line
        mov     ROWS, r9w
SG_8:   test    r8d, r8d
        pop     r9
        pop     r8
        jz      SG_SAME
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
SG_SAME:
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
SET_GEOMETRY    ENDP

;-----------------------------------------------------------------------
; After a resize, keep the cursor on screen and redraw everything.
;-----------------------------------------------------------------------
APPLY_RESIZE    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     eax, CUR_POSN
        shr     eax, 16                 ;Cursor row
        cmp     ax, ROWS                ;Still on the screen?
        jbe     AR_ROW_OK
        mov     ax, ROWS
AR_ROW_OK:
        movzx   edx, ax
        shl     edx, 16
        mov     rsi, CURSOR
        call    LOCATE                  ;Rebuild the screen around it
        mov     DIRTY_BITS, 1
        lea     rsp, [rbp]
        pop     rbp
        jmp     REDO_PROMPT
APPLY_RESIZE    ENDP

;-----------------------------------------------------------------------
; These two routines shift the display right or left to allow editing
; files which contain lines longer than the screen width.
;-----------------------------------------------------------------------
SH_RIGHT        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     LEFT_MARGIN, MAX_MARGIN ;Past max allowable margin?
        jae     SHR_RET                 ;Then can't move any more
        add     LEFT_MARGIN, 8          ;This moves the margin over
        lea     rsp, [rbp]
        pop     rbp
        jmp     SHIFTED
SHR_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
SH_RIGHT        ENDP

SH_LEFT         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     LEFT_MARGIN, 0          ;At start of line already?
        je      SHL_RET                 ;If yes, then don't shift
        sub     LEFT_MARGIN, 8          ;Move the window over
        lea     rsp, [rbp]
        pop     rbp
        jmp     SHIFTED
SHL_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
SH_LEFT         ENDP

; After a sideways shift: redraw everything, and if the cursor is now
; off the screen, move it along its line to the nearest visible column
SHIFTED         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     HSCROLL_OFF, 1          ;Don't scroll back to the cursor
        mov     DIRTY_BITS, 1           ;Redraw the screen
        call    CURSOR_COL              ;Compute column for cursor
        mov     eax, LINE_COL
        mov     r8d, LEFT_MARGIN
        cmp     eax, r8d                ;Left of the window?
        jb      SHF_LEFT
        add     eax, CUR_WIDTH
        movzx   ecx, COLUMNS
        add     ecx, r8d
        cmp     eax, ecx                ;Right of it?
        jbe     SHF_OK
        lea     eax, [rcx-1]            ;Aim for the last column
        jmp     SHF_MOVE
SHF_LEFT:
        mov     eax, r8d                ;Aim for the first column
SHF_MOVE:
        mov     SAVE_COLUMN, eax
        call    FIND_START
        mov     edx, CUR_POSN
        lea     rsp, [rbp]
        pop     rbp
        jmp     SHIFT_RIGHT
SHF_OK:
        mov     eax, LINE_COL
        mov     SAVE_COLUMN, eax        ;Save the current column
        lea     rsp, [rbp]
        pop     rbp
        ret
SHIFTED         ENDP

;-----------------------------------------------------------------------
; This moves the cursor to the top of the file.
;-----------------------------------------------------------------------
TOP             PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rax, TEXT_BEG
        mov     CURSOR, rax             ;Cursor to start of file
        mov     TOP_OF_SCREEN, rax
        xor     eax, eax
        mov     LEFT_MARGIN, eax        ;Move to far left margin
        mov     DIRTY_BITS, 1           ;Redraw the screen
        mov     CUR_POSN, eax           ;Home the cursor
        mov     SAVE_COLUMN, eax        ;Save the cursor column
        lea     rsp, [rbp]
        pop     rbp
        ret
TOP             ENDP

;-----------------------------------------------------------------------
; This moves the cursor to the bottom of the file
;-----------------------------------------------------------------------
BOTTOM          PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        movzx   edx, ROWS               ;Get screen size
        shl     edx, 16
        mov     rsi, LAST_CHAR          ;Point to last character
        mov     LEFT_MARGIN, 0          ;Set window to start of line
        call    LOCATE                  ;Adjust the screen position
        call    HOME                    ;Move cursor to start of line
        mov     DIRTY_BITS, 1           ;This will redraw the screen
        lea     rsp, [rbp]
        pop     rbp
        ret
BOTTOM          ENDP

;-----------------------------------------------------------------------
; This deletes from the cursor position to the end of line.
;-----------------------------------------------------------------------
DEL_EOL         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     LINE_FLAG, 0
        push    CURSOR                  ;Save starting cursor location
        call    ENDD                    ;Move the the end of line
        pop     rsi                     ;Get back starting cursor
        mov     rcx, CURSOR             ;End of the line
        mov     CURSOR, rsi             ;Restore starting cursor
        lea     rsp, [rbp]
        pop     rbp
        jmp     DEL_TO_BUFFER           ;Delete characters to end
DEL_EOL         ENDP

;-----------------------------------------------------------------------
; This deletes a line, placing it in the line buffer.
;-----------------------------------------------------------------------
DEL_L           PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     LINE_FLAG, 1
        call    FIND_START              ;Find start of this line
        mov     CURSOR, rsi             ;This will be the new cursor
        push    rsi                     ;Save the cursor position
        call    FIND_NEXT               ;Find the next line
        mov     rcx, rsi                ;End of the range
        pop     rsi                     ;Get back new cursor location
        lea     rsp, [rbp]
        pop     rbp
        jmp     DEL_TO_BUFFER
DEL_L           ENDP

;-----------------------------------------------------------------------
; Delete the text from RSI up to RCX into the line buffer (for F12),
; keeping at most LINE_MAX bytes, then redraw.
;-----------------------------------------------------------------------
DEL_TO_BUFFER   PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     r8, rcx                 ;R8 = end of range
        call    DIST                    ;RCX = bytes in the range
        cmp     rcx, LINE_MAX           ;Will it fit in the buffer?
        jbe     NOT_TOO_LONG
        mov     ecx, LINE_MAX           ;Only save LINE_MAX bytes,
        call    ADVANCE                 ; ending on a character
        mov     r9d, 3                  ; boundary
DE_BACK:
        cmp     r8, LAST_CHAR
        jae     NOT_TOO_LONG
        movzx   eax, byte ptr [r8]
        and     al, 0C0H
        cmp     al, 80H                 ;UTF-8 continuation byte?
        jne     NOT_TOO_LONG
        PREVP   r8
        dec     rcx
        dec     r9d
        jnz     DE_BACK
NOT_TOO_LONG:
        mov     LINE_LENGTH, rcx        ;Store length of deleted line
        test    rcx, rcx
        jz      NO_DEL_L
        mov     rcx, r8
        mov     rdi, LINE_BUFFER        ;Buffer for deleted line
        call    DELETE_RANGE            ;Save and remove the text
NO_DEL_L:
        mov     edx, CUR_POSN           ;Get cursor row/column
        mov     rsi, CURSOR             ;Get cursor offset
        call    LOCATE                  ;Adjust the screen position
        mov     DIRTY_BITS, 1           ;Redraw the screen
        lea     rsp, [rbp]
        pop     rbp
        ret
DEL_TO_BUFFER   ENDP

;-----------------------------------------------------------------------
; This undeletes a line by copying it from the line buffer into the file
;-----------------------------------------------------------------------
UDEL_L          PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     LINE_FLAG, 0            ;Is this an end of line only?
        je      UDEL_EOL                ;If yes, don't home the cursor
        call    HOME                    ;Move cursor to home
UDEL_EOL:
        mov     rax, LINE_LENGTH        ;Length of deleted line
        mov     rsi, LINE_BUFFER
        lea     rsp, [rbp]
        pop     rbp
        jmp     INSERT_STRING
UDEL_L          ENDP

;-----------------------------------------------------------------------
; These routines move the cursor left and right.
;-----------------------------------------------------------------------
LEFT            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rsi, CURSOR
        cmp     rsi, TEXT_BEG           ;At start of file?
        je      LR_NO_CHANGE            ;Then can't move left
        call    FIND_START              ;At the start of the line?
        cmp     rsi, CURSOR
        je      MOVE_UP                 ;If yes, then move up one
        mov     rsi, CURSOR
        call    PREV_CHAR               ;Back up one character
        mov     CURSOR, rsi
        lea     rsp, [rbp]
        pop     rbp
        jmp     CURSOR_MOVED
LR_NO_CHANGE:
        lea     rsp, [rbp]
        pop     rbp
        ret
MOVE_UP:
        call    UP                      ;Move up to next row
        lea     rsp, [rbp]
        pop     rbp
        jmp     ENDD                    ;And move to end of line
LEFT            ENDP

RIGHT           PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rsi, CURSOR
        cmp     rsi, LAST_CHAR          ;At end of file?
        je      RT_NO_CHANGE            ;If yes, then can't move
        cmp     byte ptr [rsi], CR      ;At end of line?
        je      MOVE_DOWN               ;If yes, then move down
        call    DECODE                  ;Step over one character
        mov     CURSOR, rsi             ;Advance the cursor
        lea     rsp, [rbp]
        pop     rbp
        jmp     CURSOR_MOVED
RT_NO_CHANGE:
        lea     rsp, [rbp]
        pop     rbp
        ret
MOVE_DOWN:
        call    HOME                    ;Move to start of line
        lea     rsp, [rbp]
        pop     rbp
        jmp     DOWN                    ;And move down one row
RIGHT           ENDP

; After LEFT or RIGHT: new cursor column, which becomes the column to
; aim for
CURSOR_MOVED    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        call    CURSOR_COL              ;Compute column for cursor
        mov     eax, LINE_COL
        mov     SAVE_COLUMN, eax        ;Save the cursor column
        lea     rsp, [rbp]
        pop     rbp
        ret
CURSOR_MOVED    ENDP

;-----------------------------------------------------------------------
; This moves the cursor to the start of the current line.
;-----------------------------------------------------------------------
HOME            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        call    FIND_START              ;Find start of line
        mov     CURSOR, rsi             ;Save the new cursor
        mov     SAVE_COLUMN, 0          ;Save the cursor column
        lea     rsp, [rbp]
        pop     rbp
        jmp     CURSOR_COL              ;Column 0, scrolled into view
HOME            ENDP

;-----------------------------------------------------------------------
; This moves the cursor to the end of the current line
;-----------------------------------------------------------------------
ENDD            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rsi, CURSOR
        call    FIND_EOL                ;Find end of this line
        mov     CURSOR, rsi             ;Store the new cursor
        call    CURSOR_COL              ;Compute the correct column
        mov     eax, LINE_COL
        mov     SAVE_COLUMN, eax        ;Save the cursor column
        lea     rsp, [rbp]
        pop     rbp
        ret
ENDD            ENDP

;-----------------------------------------------------------------------
; This moves the cursor up one row.  If the cursor is at the first row,
; the screen is scrolled down.
;-----------------------------------------------------------------------
UP              PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     edx, CUR_POSN
        mov     rsi, CURSOR
        test    edx, 0FFFF0000H         ;At top row already?
        jz      SCREEN_DN               ;If yes, then scroll down
        sub     edx, 10000H             ;Move cursor up one row
        call    FIND_CR                 ;Find the beginning of this row
        mov     CURSOR, rsi
        call    FIND_START              ;Find start of this row
        mov     CURSOR, rsi
        call    SHIFT_RIGHT             ;Skip over to current column
AT_TOP:
        lea     rsp, [rbp]
        pop     rbp
        ret
SCREEN_DN:
        mov     rsi, TOP_OF_SCREEN
        cmp     rsi, TEXT_BEG           ;At start of file?
        je      AT_TOP                  ;If at top, then do nothing
        call    FIND_PREVIOUS           ;Find the preceeding line
        mov     TOP_OF_SCREEN, rsi      ;Save new top of screen
        mov     rsi, CURSOR
        call    FIND_PREVIOUS           ;Find the preceeding line
        mov     CURSOR, rsi             ;This is the new cursor
        lea     rsp, [rbp]
        pop     rbp
        jmp     SCROLLED
UP              ENDP

; After UP or DOWN scrolls the screen: redraw everything and move along
; the new line to the saved column
SCROLLED        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     DIRTY_BITS, 1           ;Need to redraw screen
        mov     rsi, CURSOR
        mov     edx, CUR_POSN
        lea     rsp, [rbp]
        pop     rbp
        jmp     SHIFT_RIGHT             ;Move cursor to current column
SCROLLED        ENDP

;-----------------------------------------------------------------------
; This moves the cursor down one row.  When the last row is reached,
; the screen is shifted up one row.
;-----------------------------------------------------------------------
DOWN            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     edx, CUR_POSN
        mov     rsi, CURSOR             ;Get position in file
        mov     eax, edx
        shr     eax, 16
        cmp     ax, ROWS                ;At bottom row already?
        jae     SCREEN_UP               ;If at bottom, then scroll up
        call    FIND_NEXT               ;Find the start of next line
        jc      DOWN_RET                ;If no more lines, then return
        mov     CURSOR, rsi
        add     edx, 10000H             ;Advance cursor to next row
        call    SHIFT_RIGHT             ;Move cursor to current column
DOWN_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
SCREEN_UP:
        cmp     rsi, LAST_CHAR          ;Get cursor offset
        je      DOWN_RET
        call    FIND_START              ;Find the start of this line
        mov     CURSOR, rsi             ;This is the new cursor
        call    FIND_NEXT               ;Find the offset of next line
        jc      TJ_1                ;If no more lines then return
        mov     CURSOR, rsi             ;This is the new cursor
        mov     rsi, TOP_OF_SCREEN      ;Get the start of the top row
        call    FIND_NEXT               ;And find the next line
        mov     TOP_OF_SCREEN, rsi      ;Store the new top of screen
        lea     rsp, [rbp]
        pop     rbp
        jmp     SCROLLED
TJ_1:
        lea     rsp, [rbp]
        pop     rbp
        jmp     SCROLLED
DOWN            ENDP

;-----------------------------------------------------------------------
; These two routines move the screen one page at a time by calling the
; UP and DOWN procedures.
;-----------------------------------------------------------------------
PGDN            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     rax, DOWN
        mov     PAGE_PROC, rax
        lea     rsp, [rbp]
        pop     rbp
        jmp     PAGE_MOVE
PGDN            ENDP

PGUP            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     rax, UP
        mov     PAGE_PROC, rax
        lea     rsp, [rbp]
        pop     rbp
        jmp     PAGE_MOVE
PGUP            ENDP

; Call PAGE_PROC (UP or DOWN) once for each line of a page
PAGE_MOVE       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        movzx   ecx, ROWS               ;Get length of the screen
        sub     ecx, 5                  ;Don't page a full screen
        jg      PAGE_LOOP
        mov     ecx, 1
PAGE_LOOP:
        push    rcx
        call    qword ptr PAGE_PROC     ;Move the cursor down
        pop     rcx
        loop    PAGE_LOOP               ;Loop for one page length
        lea     rsp, [rbp]
        pop     rbp
        ret
PAGE_MOVE       ENDP

;-----------------------------------------------------------------------
; This toggles the insert/overstrike mode.
;-----------------------------------------------------------------------
INSERT          PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        not     INSERT_MODE             ;Toggle the switch
        lea     rsp, [rbp]
        pop     rbp
        jmp     REDO_PROMPT             ;Redraw the insert status
INSERT          ENDP

;-----------------------------------------------------------------------
; This deletes the character at the cursor.  Deleting a CR also deletes
; the LF after it and joins the lines.
;-----------------------------------------------------------------------
DEL_CHAR        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rsi, CURSOR
        cmp     rsi, LAST_CHAR          ;Are we at end of file?
        jae     NO_DEL                  ;If yes, then don't delete
        cmp     byte ptr [rsi], CR      ;Deleting a CR?
        je      COMBINE
        call    DEL_ONE_CHAR            ;Remove it
        or      DIRTY_BITS, 4           ;Current line is dirty
NO_DEL:
        lea     rsp, [rbp]
        pop     rbp
        ret
COMBINE:
        call    DEL_ONE_CHAR            ;Delete the CR
        mov     rsi, CURSOR
        cmp     rsi, LAST_CHAR
        jae     NO_DEL_LF
        cmp     byte ptr [rsi], LF      ;Was the next character a LF?
        jne     NO_DEL_LF
        call    DEL_ONE_CHAR            ;Now delete the line feed
NO_DEL_LF:
        call    CURSOR_COL
        call    DISPLAY_BOTTOM          ;Repaint bottom of the screen
        mov     eax, LINE_COL
        mov     SAVE_COLUMN, eax        ;Save the cursor column
        lea     rsp, [rbp]
        pop     rbp
        ret
DEL_CHAR        ENDP

;-----------------------------------------------------------------------
; Delete the character at the cursor.
;-----------------------------------------------------------------------
DEL_ONE_CHAR    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rsi, CURSOR
        call    DECODE                  ;Find the end of the character
        mov     rcx, rsi
        mov     rsi, CURSOR
        xor     edi, edi
        lea     rsp, [rbp]
        pop     rbp
        jmp     DELETE_RANGE
DEL_ONE_CHAR    ENDP

;-----------------------------------------------------------------------
; This toggles the mark state and resets the paste buffer pointers.
;-----------------------------------------------------------------------
MARK            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        xor     eax, eax
        not     MARK_MODE               ;Toggle the mode flag
        cmp     MARK_MODE, al           ;Turning mode ON?
        jne     MARK_ON
        mov     DIRTY_BITS, 1           ;Need to redraw the screen
        mov     MARK_START, -1
        jmp     MARK_RET
MARK_ON:
        mov     rax, CURSOR             ;Get the cursor offset
        mov     MARK_START, rax         ;Start of marked range
MARK_RET:
        mov     MARK_END, rax           ;End of marked range
        mov     MARK_HOME, rax          ;Center of marked range
        lea     rsp, [rbp]
        pop     rbp
        ret
MARK            ENDP

;-----------------------------------------------------------------------
; This removes the marked text and places it in the paste buffer
; (F5 or Ctrl-X)
;-----------------------------------------------------------------------
CUT             PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     MARK_MODE, 0            ;Is the mark mode on?
        je      NO_MARK                 ;If not, then do nothing
        mov     rcx, MARK_END           ;Get end of mark region
        mov     rsi, MARK_START         ;Get start of mark region
        call    DIST                    ;Number of bytes selected
        mov     PASTE_SIZE, rcx
        test    rcx, rcx
        jz      NO_MARK
        call    GROW_PASTE              ;Room in the paste buffer?
        jc      TJ_2
        mov     rsi, MARK_START
        mov     rcx, MARK_END
        mov     rdi, PASTE_SEG          ;Copy to the paste buffer
        call    DELETE_RANGE            ; and remove from the file
        call    CLIP_PUT                ;And to the Windows clipboard
        mov     edx, CUR_POSN
        mov     rsi, MARK_START
        call    LOCATE                  ;Adjust the screen position
        call    MARK                    ;This turns off select
NO_MARK:
        lea     rsp, [rbp]
        pop     rbp
        ret
TJ_2:
        lea     rsp, [rbp]
        pop     rbp
        jmp     PASTE_NO_ROOM
CUT             ENDP

; CUT or COPY found no memory for the paste buffer
PASTE_NO_ROOM   PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     PASTE_SIZE, 0           ;Not enough memory to hold it
        lea     rsp, [rbp]
        pop     rbp
        jmp     BEEP
PASTE_NO_ROOM   ENDP

;-----------------------------------------------------------------------
; This copies the marked text into the paste buffer without removing it
; from the file, then turns off the mark state.  (F3 or Ctrl-C)
;-----------------------------------------------------------------------
COPY            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     MARK_MODE, 0            ;Is the mark mode on?
        je      NO_COPY                 ;If not, then do nothing
        mov     rcx, MARK_END           ;Get end of mark region
        mov     rsi, MARK_START         ;Get start of mark region
        call    DIST                    ;Number of bytes selected
        test    rcx, rcx
        jz      COPY_DONE               ;Nothing marked, keep old paste
        call    GROW_PASTE              ;Room in the paste buffer?
        jc      TJ_3
        mov     PASTE_SIZE, rcx
        mov     rsi, MARK_START
        mov     rcx, MARK_END
        mov     rdi, PASTE_SEG
        call    COPY_OUT                ;Copy marked text to buffer
        call    CLIP_PUT                ;And to the Windows clipboard
COPY_DONE:
        call    MARK                    ;This turns off select
NO_COPY:
        lea     rsp, [rbp]
        pop     rbp
        ret
TJ_3:
        lea     rsp, [rbp]
        pop     rbp
        jmp     PASTE_NO_ROOM
COPY            ENDP

;-----------------------------------------------------------------------
; Put the paste buffer on the Windows clipboard as Unicode text, so
; other programs (and the console's own Ctrl+V) get what was copied.
; The clipboard needs an owner window: a hidden one is made for the
; moment and destroyed again, so no window messages are ever waiting
; for this program.  Nothing happens if the clipboard is busy or the
; text is very large.
;-----------------------------------------------------------------------
CLIP_PUT        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rbx
        push    r12
        push    r13
        mov     r13, PASTE_SIZE
        test    r13, r13
        jz      CPU_RET
        cmp     r13, CLIP_MAX           ;Too big for the clipboard?
        ja      CPU_RET
        mov     rsi, PASTE_SEG
        mov     byte ptr [rsi+r13], 0   ;Stops DECODE_STR at the end
        mov     rbx, rsp                ;CreateWindowExW has twelve
        and     rsp, -16                ; arguments
        sub     rsp, 96
        xor     ecx, ecx                ;No extended style
        lea     rdx, STATIC_CLASS       ;A ready-made window class
        xor     r8d, r8d                ;No title
        xor     r9d, r9d                ;Not visible
        xor     eax, eax
        mov     [rsp+32], rax           ;Position, size, parent,
        mov     [rsp+40], rax           ; menu, instance, parameter
        mov     [rsp+48], rax
        mov     [rsp+56], rax
        mov     [rsp+64], rax
        mov     [rsp+72], rax
        mov     [rsp+80], rax
        mov     [rsp+88], rax
        call    CreateWindowExW
        mov     rsp, rbx
        mov     CLIP_HWND, rax
        test    rax, rax
        jz      CPU_RET
        mov     r12d, 10                ;Another program may have the
CPU_OPEN:                               ; clipboard open: try a few
        mov     rcx, CLIP_HWND          ; times
        CALLWIN OpenClipboard
        test    eax, eax
        jnz     CPU_OPENED
        mov     ecx, 20
        CALLWIN Sleep
        dec     r12d
        jnz     CPU_OPEN
        jmp     CPU_DESTROY
CPU_OPENED:
        CALLWIN EmptyClipboard
        lea     rdx, [r13*2+2]          ;UTF-16 never needs more
        mov     ecx, GMEM_MOVEABLE
        CALLWIN GlobalAlloc
        mov     r12, rax
        test    rax, rax
        jz      CPU_CLOSE
        mov     rcx, r12
        CALLWIN GlobalLock
        test    rax, rax
        jz      CPU_FREE
        mov     rdi, rax
        mov     rsi, PASTE_SEG
        add     r13, rsi                ;R13 = end of the text
CPU_LOOP:
        cmp     rsi, r13
        jae     CPU_END
        call    DECODE_STR              ;Next character (a byte that
        cmp     eax, 0FFFFH             ; isn't UTF-8 counts as Latin-1)
        jbe     CPU_UNIT
        sub     eax, 10000H             ;Surrogate pair
        mov     ecx, eax
        shr     eax, 10
        add     eax, 0D800H
        mov     [rdi], ax
        add     rdi, 2
        mov     eax, ecx
        and     eax, 3FFH
        add     eax, 0DC00H
CPU_UNIT:
        mov     [rdi], ax
        add     rdi, 2
        jmp     CPU_LOOP
CPU_END:
        mov     word ptr [rdi], 0
        mov     rcx, r12
        CALLWIN GlobalUnlock
        mov     ecx, CF_UNICODETEXT
        mov     rdx, r12
        CALLWIN SetClipboardData        ;Windows owns the memory now
        test    rax, rax
        jnz     CPU_SET
CPU_FREE:
        mov     rcx, r12
        CALLWIN GlobalFree
CPU_CLOSE:
        CALLWIN CloseClipboard
        jmp     CPU_DESTROY
CPU_SET:
        CALLWIN CloseClipboard
        CALLWIN GetClipboardSequenceNumber
        mov     CLIP_SEQ, eax           ;The clipboard holds our text
CPU_DESTROY:
        mov     rcx, CLIP_HWND
        CALLWIN DestroyWindow
CPU_RET:
        pop     r13
        pop     r12
        pop     rbx
        lea     rsp, [rbp]
        pop     rbp
        ret
CLIP_PUT        ENDP

;-----------------------------------------------------------------------
; Before a paste: if another program has put text on the clipboard
; since TED last put or took it, take it into the paste buffer, as
; UTF-8 with CR/LF line ends.  If the clipboard still holds TED's own
; text, or holds no text, the paste buffer is used as it is (so text
; TED copied pastes back byte for byte).
;-----------------------------------------------------------------------
CLIP_GET        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rbx
        push    r12
        push    r13
        CALLWIN GetClipboardSequenceNumber
        cmp     eax, CLIP_SEQ           ;Still what we put there?
        je      CG_RET
        xor     ecx, ecx
        CALLWIN OpenClipboard
        test    eax, eax
        jz      CG_RET                  ;Busy: use the paste buffer
        mov     ecx, CF_UNICODETEXT
        CALLWIN GetClipboardData
        mov     r12, rax
        test    rax, rax
        jz      CG_CLOSE                ;No text there
        mov     rcx, r12
        CALLWIN GlobalLock
        mov     r13, rax                ;R13 = the UTF-16 text
        test    rax, rax
        jz      CG_CLOSE
        mov     rcx, r12
        CALLWIN GlobalSize
        shr     rax, 1                  ;Most units there can be
        mov     rbx, rax
        lea     rcx, [rax*2+rax]        ;Three UTF-8 bytes for each
        call    GROW_PASTE
        jc      CG_UNLOCK
        mov     rsi, r13
        lea     rbx, [r13+rbx*2]        ;RBX = end of the clipboard data
        mov     rdi, PASTE_SEG
CG_LOOP:
        cmp     rsi, rbx
        jae     CG_END
        movzx   eax, word ptr [rsi]
        test    eax, eax                ;The text ends with a zero
        jz      CG_END
        add     rsi, 2
        cmp     eax, CR
        je      CG_CR
        cmp     eax, LF
        je      CG_NEWLINE              ;A lone LF: make it CR/LF
        mov     ecx, eax
        and     ecx, 0FC00H
        cmp     ecx, 0DC00H             ;A lone low surrogate
        je      CG_BAD
        cmp     ecx, 0D800H
        jne     CG_PUT
        cmp     rsi, rbx                ;High surrogate: join it to
        jae     CG_BAD                  ; the low one after it
        movzx   ecx, word ptr [rsi]
        mov     edx, ecx
        and     edx, 0FC00H
        cmp     edx, 0DC00H
        jne     CG_BAD
        add     rsi, 2
        sub     eax, 0D800H
        shl     eax, 10
        sub     ecx, 0DC00H
        add     eax, ecx
        add     eax, 10000H
        jmp     CG_PUT
CG_BAD:
        mov     eax, 0FFFDH
CG_PUT:
        call    ENCODE_UTF8
        add     rdi, rcx
        jmp     CG_LOOP
CG_CR:
        cmp     rsi, rbx                ;CR/LF stays; a lone CR gets
        jae     CG_NEWLINE              ; an LF
        cmp     word ptr [rsi], LF
        jne     CG_NEWLINE
        add     rsi, 2
CG_NEWLINE:
        mov     word ptr [rdi], LF*256+CR
        add     rdi, 2
        jmp     CG_LOOP
CG_END:
        sub     rdi, PASTE_SEG
        mov     PASTE_SIZE, rdi
CG_UNLOCK:
        mov     rcx, r12
        CALLWIN GlobalUnlock
CG_CLOSE:
        CALLWIN CloseClipboard
        CALLWIN GetClipboardSequenceNumber
        mov     CLIP_SEQ, eax           ;Taken: reuse it next time
CG_RET:
        pop     r13
        pop     r12
        pop     rbx
        lea     rsp, [rbp]
        pop     rbp
        ret
CLIP_GET        ENDP

;-----------------------------------------------------------------------
; This copies the paste buffer into the file at the cursor location
; (F6 or Ctrl-V).  The paste buffer is shared with the Windows
; clipboard: CUT and COPY put text there too, and PASTE takes text
; that another program put there.
;-----------------------------------------------------------------------
PASTE           PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        call    CLIP_GET                ;Text from another program?
        mov     rcx, PASTE_SIZE         ;Number of characters in buffer
        test    rcx, rcx                ;Any there?
        jz      NO_PASTE                ;If not, nothing to paste
        mov     rsi, PASTE_SEG
        call    INSERT_BYTES            ;Copy in the new characters
        jc      NO_PASTE                ;If no room, just exit
        mov     CURSOR, rax             ;Cursor moved to end of insert
        mov     rsi, rax
        mov     edx, CUR_POSN           ;Get current cursor row
        call    LOCATE                  ;Adjust the screen position
        mov     DIRTY_BITS, 1           ;Redraw the screen
NO_PASTE:
        lea     rsp, [rbp]
        pop     rbp
        ret
PASTE           ENDP

;-----------------------------------------------------------------------
; UNDO (F2 or Ctrl-Z) takes back the last change; REDO (Ctrl-Y) puts
; back the last change undone.  A change is everything one command did
; (a run of typing, or of erasing, counts as one).
;-----------------------------------------------------------------------
UNDO            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     rbx, HIST_UNDO
        mov     LOG_MODE, 1             ;Changes made go on the redo stack
        lea     rsp, [rbp]
        pop     rbp
        jmp     UNDO_REDO
UNDO            ENDP

REDO            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     rbx, HIST_REDO
        mov     LOG_MODE, 2             ;Changes made go on the undo stack
        lea     rsp, [rbp]
        pop     rbp
        jmp     UNDO_REDO
REDO            ENDP

; Apply, in reverse, every record of the top group of the stack at RBX
UNDO_REDO       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     qword ptr [rbx+8], 0    ;Anything there?
        je      UR_EMPTY
        cmp     MARK_MODE, 0            ;End any selection first
        je      UR_NOMARK
        call    MARK
UR_NOMARK:
        mov     rax, [rbx]
        add     rax, [rbx+8]
        mov     rax, [rax-REC_SIZE+24]
        mov     UR_GROUP, rax           ;The group to take back
UR_LOOP:
        mov     rcx, [rbx+8]
        test    rcx, rcx
        jz      UR_END
        mov     r8, [rbx]
        add     r8, rcx                 ;R8 = end of the top record
        mov     rax, [r8-REC_SIZE+24]
        cmp     rax, UR_GROUP           ;Still the same group?
        jne     UR_END
        mov     LOG_GROUP, rax          ;The opposite change keeps the
        mov     rax, [r8-REC_SIZE+16]   ; same group and serial number
        mov     LOG_SERIAL, rax
        mov     eax, REC_SIZE           ;Take the record off the stack
        cmp     byte ptr [r8-REC_SIZE+32], REC_INS
        je      UR_SIZED
        add     rax, [r8-REC_SIZE+8]    ;With its text
UR_SIZED:
        mov     UR_SIZE, rax
        sub     [rbx+8], rax
        mov     rax, [r8-REC_SIZE]      ;Where the change was
        call    OFFSET_PTR
        mov     CURSOR, rsi
        mov     rcx, [r8-REC_SIZE+8]    ;How many bytes
        cmp     byte ptr [r8-REC_SIZE+32], REC_INS
        jne     UR_REINSERT
        call    ADVANCE                 ;Text was inserted: delete it
        mov     rcx, r8
        xor     edi, edi
        call    DELETE_RANGE
        jmp     UR_LOOP
UR_REINSERT:
        lea     rsi, [r8-REC_SIZE]      ;Text was deleted: put it back
        sub     rsi, rcx
        call    INSERT_BYTES
        jc      UR_FAIL
        mov     CURSOR, rax             ;Cursor after the restored text
        jmp     UR_LOOP
UR_FAIL:
        mov     rax, UR_SIZE            ;No room: leave the record there
        add     [rbx+8], rax
UR_END:
        mov     LOG_MODE, 0
        mov     START_GROUP, 1
        mov     LAST_CLASS, 0
        call    UPDATE_MODIFIED
        mov     edx, CUR_POSN
        mov     rsi, CURSOR
        call    LOCATE                  ;Adjust the screen position
        mov     DIRTY_BITS, 1           ;Redraw the screen
        lea     rsp, [rbp]
        pop     rbp
        ret
UR_EMPTY:
        mov     LOG_MODE, 0
        lea     rsp, [rbp]
        pop     rbp
        jmp     BEEP
UNDO_REDO       ENDP

;-----------------------------------------------------------------------
; Note a change in the history.  EAX = REC_INS or REC_DEL, RDX = its
; offset in the text, RCX = bytes, RSI = the deleted bytes (REC_DEL).
; A change made while undoing goes on the redo stack and one made while
; redoing on the undo stack, each keeping the serial number and group
; of the change taken back.  An ordinary change empties the redo stack
; and joins up with the last record where it can.  If the history is
; full, it is emptied.  All registers saved.
;-----------------------------------------------------------------------
LOG_EDIT        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        PUSH_VOLATILE
        push    rsi
        push    rdi
        push    rbx
        mov     r10d, eax               ;R10 = type
        cmp     HIST_UNDO, 0            ;Any history kept?
        je      LE_NONE
        lea     rbx, HIST_UNDO
        cmp     LOG_MODE, 1
        jne     LE_STACK
        lea     rbx, HIST_REDO
LE_STACK:
        mov     r11, LOG_SERIAL
        mov     rax, LOG_GROUP
        cmp     LOG_MODE, 0
        jne     LE_NEW
        mov     qword ptr HIST_REDO+8, 0 ;A new change: no more redo
        inc     NEXT_SERIAL
        mov     r11, NEXT_SERIAL        ;R11 = serial number
        cmp     START_GROUP, 0
        je      LE_SAME
        mov     START_GROUP, 0
        mov     CUR_GROUP, r11          ;New group, named by its first
        mov     rax, r11                ; serial number
        jmp     LE_NEW
LE_SAME:
        mov     rax, CUR_GROUP
        mov     r8, [rbx+8]
        test    r8, r8
        jz      LE_NEW
        add     r8, [rbx]               ;R8 = end of the top record
        cmp     [r8-REC_SIZE+24], rax   ;Same group and type?
        jne     LE_NEW
        cmp     byte ptr [r8-REC_SIZE+32], r10b
        jne     LE_NEW
        cmp     r10d, REC_INS
        jne     LE_MDEL
        mov     r9, [r8-REC_SIZE]       ;Typing on: right after the
        add     r9, [r8-REC_SIZE+8]     ; last insertion?
        cmp     r9, rdx
        jne     LE_NEW
        add     [r8-REC_SIZE+8], rcx    ;Make that one longer
        mov     [r8-REC_SIZE+16], r11
        jmp     LE_DONE
LE_MDEL:
        cmp     [r8-REC_SIZE], rdx      ;Deleting at the same place?
        jne     LE_NEW
        mov     r9, [rbx+8]
        add     r9, rcx
        call    HIST_ROOM
        jc      LE_FULL
        push    rsi
        push    rcx
        lea     rsi, [r8-REC_SIZE]      ;Footer out of the way
        lea     rdi, FOOT_TMP
        mov     ecx, REC_SIZE / 8
        rep     movsq
        pop     rcx
        pop     rsi
        lea     rdi, [r8-REC_SIZE]      ;New bytes after the old ones
        add     [rbx+8], rcx
        add     FOOT_TMP+8, rcx
        rep     movsb
        mov     FOOT_TMP+16, r11
        lea     rsi, FOOT_TMP           ;And the footer after them
        mov     ecx, REC_SIZE / 8
        rep     movsq
        jmp     LE_DONE
LE_NEW:
        mov     r8, rax                 ;R8 = group
        mov     r9, [rbx+8]             ;Room for the record?
        add     r9, REC_SIZE
        cmp     r10d, REC_INS
        je      LE_ROOM
        add     r9, rcx
LE_ROOM:
        call    HIST_ROOM
        jc      LE_FULL
        mov     rdi, [rbx]
        add     rdi, [rbx+8]
        cmp     r10d, REC_INS
        je      LE_FOOT
        push    rcx
        rep     movsb                   ;The deleted bytes
        pop     rcx
LE_FOOT:
        mov     [rdi], rdx
        mov     [rdi+8], rcx
        mov     [rdi+16], r11
        mov     [rdi+24], r8
        mov     [rdi+32], r10
        add     rdi, REC_SIZE
        sub     rdi, [rbx]
        mov     [rbx+8], rdi            ;New top of the stack
LE_DONE:
        call    UPDATE_MODIFIED
        pop     rbx
        pop     rdi
        pop     rsi
        POP_VOLATILE
        lea     rsp, [rbp]
        pop     rbp
        ret
LE_FULL:
        mov     qword ptr [rbx+8], 0    ;Too much: forget that stack
        lea     rax, HIST_UNDO
        cmp     rbx, rax
        jne     LE_DONE
LE_NONE:
        inc     NEXT_SERIAL             ;The text no longer matches any
        mov     rax, NEXT_SERIAL        ; state in the history
        mov     EMPTY_ID, rax
        jmp     LE_DONE
LOG_EDIT        ENDP

; Make sure the history stack at RBX can hold R9 bytes.  CF=1 if not.
HIST_ROOM       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rcx
        push    rdx
        push    r8
        mov     rcx, [rbx]
        mov     rdx, r9
        lea     r8, [rbx+16]
        call    COMMIT_BUF
        pop     r8
        pop     rdx
        pop     rcx
        lea     rsp, [rbp]
        pop     rbp
        ret
HIST_ROOM       ENDP

;-----------------------------------------------------------------------
; Return in RAX the number of the text's current state: the serial
; number of the top undo record, or EMPTY_ID.  Destroys RCX.
;-----------------------------------------------------------------------
STATE_ID        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rax, EMPTY_ID
        mov     rcx, HIST_UNDO+8
        test    rcx, rcx
        jz      SI_RET
        add     rcx, HIST_UNDO
        mov     rax, [rcx-REC_SIZE+16]
SI_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
STATE_ID        ENDP

; Set MODIFIED (and redraw the "*") from the state number.  Saves all.
UPDATE_MODIFIED PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rax
        push    rcx
        call    STATE_ID
        xor     ecx, ecx
        cmp     rax, SAVED_SERIAL
        setne   cl
        cmp     MODIFIED, cl
        je      UM_RET
        mov     MODIFIED, cl
        mov     PROMPT_DIRTY, 1
UM_RET:
        pop     rcx
        pop     rax
        lea     rsp, [rbp]
        pop     rbp
        ret
UPDATE_MODIFIED ENDP

; The text now matches the file on disk
MARK_CLEAN      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rcx
        call    STATE_ID
        mov     SAVED_SERIAL, rax
        mov     MODIFIED, 0
        mov     PROMPT_DIRTY, 1
        pop     rcx
        lea     rsp, [rbp]
        pop     rbp
        ret
MARK_CLEAN      ENDP

;-----------------------------------------------------------------------
; Note what kind of key is about to be handled (AL, AH and KEY_CHAR as
; from GET_KEY), so that runs of typing or erasing are undone as one
; change.  A space or Enter ends a run of typing, unless more keys are
; already waiting (text pasted by the console).  Saves all but flags.
;-----------------------------------------------------------------------
CLASSIFY        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rcx
        xor     ecx, ecx                ;0 = any other command
        cmp     KEY_CHAR, 80H
        jae     CL_TYPE
        test    al, al
        jnz     CL_ASCII
        cmp     ah, 53H                 ;Del
        jne     CL_SET
        mov     cl, 2                   ;2 = erasing
        jmp     CL_SET
CL_ASCII:
        cmp     ah, 0EH                 ;Backspace
        jne     CL_A2
        mov     cl, 2
        jmp     CL_SET
CL_A2:
        cmp     al, " "
        je      CL_SPACE
        cmp     al, CR
        je      CL_SPACE
        cmp     al, LF                  ;(Pasted after a CR)
        je      CL_SPACE
        cmp     al, TAB
        je      CL_TYPE
        cmp     al, " "
        jb      CL_SET                  ;Control keys
        cmp     al, 7FH
        je      CL_SET
CL_TYPE:
        mov     cl, 1                   ;1 = typing
        jmp     CL_SET
CL_SPACE:
        mov     cl, 3                   ;3 = space or Enter
CL_SET:
        mov     ch, LAST_CLASS
        mov     LAST_CLASS, cl
        test    cl, cl
        jz      CL_NEW
        cmp     cl, ch                  ;More of the same?
        je      CL_RET
        cmp     cl, 2                   ;Typing that arrives faster than
        je      CL_NOT_PASTE            ; anyone types (the console
        cmp     ch, 1                   ; pasting) stays one change
        je      CL_BURST
        cmp     ch, 3
        jne     CL_NOT_PASTE
CL_BURST:
        push    rax
        call    KEY_READY
        test    eax, eax
        pop     rax
        jnz     CL_RET
CL_NOT_PASTE:
        cmp     cl, 3                   ;A space after typing?
        jne     CL_NEW
        cmp     ch, 1
        je      CL_RET
CL_NEW:
        mov     START_GROUP, 1
CL_RET:
        pop     rcx
        lea     rsp, [rbp]
        pop     rbp
        ret
CLASSIFY        ENDP

;-----------------------------------------------------------------------
; Return in RSI the position of text offset RAX.  Destroys nothing else.
;-----------------------------------------------------------------------
OFFSET_PTR      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rsi, GAP_START
        sub     rsi, r15                ;Bytes before the gap
        cmp     rax, rsi
        lea     rsi, [r15+rax]
        jb      OP_RET
        add     rsi, GAP_END            ;After it: skip the gap
        sub     rsi, GAP_START
OP_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
OFFSET_PTR      ENDP

;-----------------------------------------------------------------------
; This inserts RAX bytes from RSI into the file at the cursor.  The
; cursor stays at the start of the inserted text.
;-----------------------------------------------------------------------
INSERT_STRING   PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rcx, rax
        call    INSERT_BYTES            ;Copy the characters in
        jc      NO_SPACE                ;If no space available, exit
        mov     rsi, CURSOR             ;Get the new cursor offset
        mov     edx, CUR_POSN           ;Also get the current row
        mov     DIRTY_BITS, 1           ;And redraw the screen
        call    LOCATE                  ;Adjust the screen position
NO_SPACE:
        lea     rsp, [rbp]
        pop     rbp
        ret
INSERT_STRING   ENDP

;-----------------------------------------------------------------------
; This prompts for a verify keystroke then exits without saving the file
;-----------------------------------------------------------------------
ABORT           PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     MODIFIED, 0             ;Anything to lose?
        je      TJ_4                  ;If not, just quit
        movzx   edx, ROWS
        inc     edx                     ;Bottom row of screen
        shl     edx, 16                 ;First column
        lea     rsi, VERIFY_MESS
        call    TTY_STRING              ;Display verify message

        call    GET_KEY                 ;Read the next key
        or      al, 32                  ;Convert to lower case
        cmp     al, "y"                 ;Was answer Yes?
        je      TJ_5                  ;If yes, then were finished
        lea     rsp, [rbp]
        pop     rbp
        jmp     REDO_PROMPT             ;If not, redraw the prompt
TJ_4:
        lea     rsp, [rbp]
        pop     rbp
        jmp     FINISH
TJ_5:
        lea     rsp, [rbp]
        pop     rbp
        jmp     FINISH
ABORT           ENDP

;-----------------------------------------------------------------------
; FINISH clears the prompt line and quits.  QUIT restores the console
; and ends the program; it is also used for errors and when console
; input is lost.  Neither returns.
;-----------------------------------------------------------------------
FINISH          PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        movzx   edx, ROWS               ;Move to last row on screen
        shl     edx, 16                 ;And column zero
        call    SET_CURSOR
        add     edx, 10000H
        call    ERASE_EOL               ;Erase the last row
        lea     rsp, [rbp]
        pop     rbp
        jmp     QUIT
FINISH          ENDP

QUIT            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     OWN_SCREEN, 0           ;Using our own screen buffer?
        je      QUIT_SAME_SCREEN
        mov     rcx, H_ORIG             ;Bring back the user's screen
        CALLWIN SetConsoleActiveScreenBuffer
        mov     rcx, H_OUT
        CALLWIN CloseHandle
        mov     rax, H_ORIG
        mov     H_OUT, rax              ;Print the banner there
        jmp     NO_FLUSH
QUIT_SAME_SCREEN:
        cmp     SCREEN_READY, 0         ;Has the editor drawn anything?
        je      NO_FLUSH
        call    FLUSH_SCREEN            ;Leave the final screen showing
NO_FLUSH:
        lea     rsi, COPYRIGHT
        call    PRINT_STR               ;Display string
        cmp     MODE_SAVED, 0
        je      NO_RESTORE
        mov     rcx, H_IN
        mov     edx, OLD_IN_MODE
        CALLWIN SetConsoleMode          ;Restore keyboard mode
NO_RESTORE:
        mov     ecx, EXIT_CODE          ;0, or 1 after an error
        CALLWIN ExitProcess
QUIT            ENDP

;-----------------------------------------------------------------------
; EXIT (F7 or Ctrl-Q) saves the file and quits.  With no unsaved changes it just
; quits.  If the save is cancelled with Escape, editing continues.
;-----------------------------------------------------------------------
EXIT            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     MODIFIED, 0             ;Anything to save?
        je      TJ_6                  ;If not, just quit
        call    SAVE_FILE               ;Ask for the name and save
        jnc     TJ_7                  ;Saved - all done
        lea     rsp, [rbp]
        pop     rbp
        ret
TJ_6:
        lea     rsp, [rbp]
        pop     rbp
        jmp     FINISH
TJ_7:
        lea     rsp, [rbp]
        pop     rbp
        jmp     FINISH
EXIT            ENDP

;-----------------------------------------------------------------------
; SAVE (F10) saves the file and carries on editing.
;-----------------------------------------------------------------------
SAVE            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        call    SAVE_FILE               ;Ask for the name and save
        jc      SAVE_RET                ;Cancelled
        call    MARK_CLEAN              ;No unsaved changes now
        call    REDO_PROMPT             ;Clear the "*"
        lea     rsi, SAVED_MESS
        call    SHOW_MESSAGE            ;"Saved" until the next key
SAVE_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
SAVE            ENDP

;-----------------------------------------------------------------------
; This prompts for a filename (any Unicode name may be typed) and
; saves the file.  The new text is written to name.$$$ first, so a
; failed write never harms the original.  It is then put in place:
;   - a new file: name.$$$ is renamed to name
;   - normally: ReplaceFile swaps it in, keeping the original's
;     permissions (ACLs), attributes, streams and creation time, and
;     turns the original into name.BAK
;   - a file with hard links, or a drive without ReplaceFile: the
;     original is copied to name.BAK and then rewritten in place, which
;     keeps the links and everything else about the file
; Problems are explained on the prompt line before asking again.
; Returns CF=0 once the file is saved, or CF=1 if the user pressed
; Escape (the prompt line is then redrawn).
;-----------------------------------------------------------------------
SAVE_FILE       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
NEXT_TRY:
        lea     rsi, SAVE_MESS          ;Let the user edit the name
        mov     rdi, NAME_POINTER
        mov     r8d, MAX_NAME - 6
        mov     r9d, 1                  ;Any characters allowed
        call    EDIT_FIELD
        jnc     GOT_NAME
        mov     DIRTY_BITS, 1           ;Escape - redraw the screen
        call    REDO_PROMPT             ;Redraw the prompt
        stc                             ;Not saved
        lea     rsp, [rbp]
        pop     rbp
        ret
GOT_NAME:
        mov     rsi, NAME_POINTER       ;Point to the filename
        cmp     byte ptr [rsi], 0       ;Empty name?
        je      BAD_NAME
        lea     rdi, WNAME              ;UTF-16 copies of the name, the
        call    TO_WIDE                 ; temporary name and the backup
        lea     rsi, DOT_TMP            ; name
        lea     rdi, NAME_TMP
        call    CHG_EXTENSION
        lea     rsi, NAME_TMP
        lea     rdi, WTMP
        call    TO_WIDE
        lea     rsi, DOT_BAK
        lea     rdi, NAME_BAK
        call    CHG_EXTENSION
        lea     rsi, NAME_BAK
        lea     rdi, WBAK
        call    TO_WIDE
        mov     FILE_EXISTS, 0
        mov     FILE_LINKS, 1
        lea     rcx, WNAME
        CALLWIN GetFileAttributesW      ;Is the file there already?
        cmp     eax, INVALID_FILE_ATTRIBUTES
        jne     NAME_EXISTS
        CALLWIN GetLastError
        lea     rsi, NO_PATH_MESS
        cmp     eax, ERROR_PATH_NOT_FOUND  ;No such folder?
        je      SAVE_PROBLEM
        jmp     NAME_OK                 ;A new file
NAME_EXISTS:
        lea     rsi, FOLDER_SAVE_MESS
        test    eax, FILE_ATTRIBUTE_DIRECTORY
        jnz     SAVE_PROBLEM            ;Can't save over a folder
        lea     rsi, READ_ONLY_MESS
        test    eax, FILE_ATTRIBUTE_READONLY
        jnz     SAVE_PROBLEM            ;Read-only: don't replace it
        mov     FILE_EXISTS, 1
        lea     rcx, WBAK               ;Can the old .BAK be replaced?
        CALLWIN GetFileAttributesW
        cmp     eax, INVALID_FILE_ATTRIBUTES
        je      BAK_OK                  ;There isn't one
        lea     rsi, BAK_RO_MESS
        test    eax, FILE_ATTRIBUTE_READONLY or FILE_ATTRIBUTE_DIRECTORY
        jnz     SAVE_PROBLEM
BAK_OK:
        call    GET_INFO                ;Which file is it, how many
        jc      INFO_DONE               ; hard links does it have?
        mov     eax, dword ptr BHFI+40  ;nNumberOfLinks
        mov     FILE_LINKS, eax
        call    CHECK_STAMP             ;Changed by someone else since
        jnc     INFO_DONE               ; we loaded or saved it?
        lea     rsi, CHANGED_MESS
        call    SHOW_MESSAGE
        call    BEEP
        call    GET_KEY
        mov     PROMPT_MSG, 0
        or      al, 32                  ;Convert to lower case
        cmp     al, "y"                 ;Overwrite their changes?
        jne     NEXT_TRY                ;If not, ask for a name again
INFO_DONE:
NAME_OK:
        call    PREPARE_OUTPUT          ;The bytes to write
        lea     rsi, NO_MEM_SAVE_MESS
        jc      SAVE_PROBLEM
        lea     rcx, WTMP               ;Write them to name.$$$
        mov     edx, CREATE_ALWAYS
        call    WRITE_FILE
        jnc     TMP_WRITTEN
        lea     rcx, WTMP
        CALLWIN DeleteFileW             ;Remove any partial copy
        lea     rsi, CANT_CREATE_MESS
        jmp     SAVE_PROBLEM
TMP_WRITTEN:
        cmp     FILE_EXISTS, 0          ;A new file?
        jne     REPLACE_OLD
        lea     rcx, WTMP               ;Then just rename ours
        lea     rdx, WNAME
        CALLWIN MoveFileW
        test    eax, eax
        jnz     SAVED_OK
        lea     rcx, WTMP
        CALLWIN DeleteFileW
        lea     rsi, CANT_CREATE_MESS
        jmp     SAVE_PROBLEM
REPLACE_OLD:
        lea     rcx, WBAK               ;The old backup goes
        CALLWIN DeleteFileW
        lea     rcx, WBAK
        CALLWIN GetFileAttributesW      ;Is it really gone?
        cmp     eax, INVALID_FILE_ATTRIBUTES
        je      BAK_GONE
        lea     rcx, WTMP               ;No (in use, perhaps): the
        CALLWIN DeleteFileW             ; original is untouched, so
        lea     rsi, BAK_STUCK_MESS     ; remove our copy and say so
        jmp     SAVE_PROBLEM
BAK_GONE:
        cmp     FILE_LINKS, 1           ;Hard links: replacing the file
        ja      IN_PLACE                ; would break them
        lea     rcx, WNAME              ;File being replaced
        lea     rdx, WTMP               ;The new contents
        lea     r8, WBAK                ;Where the old one goes
        mov     r9d, REPLACEFILE_IGNORE_MERGE_ERRORS
        CALLWIN ReplaceFileW, 0, 0
        test    eax, eax
        jnz     SAVED_OK
        lea     rcx, WNAME              ;Didn't work.  Is the file there?
        CALLWIN GetFileAttributesW
        cmp     eax, INVALID_FILE_ATTRIBUTES
        jne     IN_PLACE                ;Yes: no ReplaceFile here
        lea     rcx, WTMP               ;No: it became the .BAK, so put
        lea     rdx, WNAME              ; ours in its place
        CALLWIN MoveFileW
        test    eax, eax
        jnz     SAVED_OK
        lea     rcx, WNAME              ;Or write the text there directly
        mov     edx, CREATE_NEW
        call    WRITE_FILE
        jnc     DELETE_TMP
        jmp     TEXT_IN_TMP
IN_PLACE:
        lea     rcx, WNAME              ;Copy the original to .BAK, then
        lea     rdx, WBAK               ; rewrite the file itself
        xor     r8d, r8d                ;(Overwrite any .BAK)
        CALLWIN CopyFileW
        test    eax, eax
        jnz     BAK_COPIED
        lea     rcx, WTMP
        CALLWIN DeleteFileW
        lea     rsi, BAK_COPY_MESS
        jmp     SAVE_PROBLEM
BAK_COPIED:
        lea     rcx, WNAME
        mov     edx, TRUNCATE_EXISTING
        call    WRITE_FILE
        jc      TEXT_IN_TMP             ;The original is in .BAK
DELETE_TMP:
        lea     rcx, WTMP
        CALLWIN DeleteFileW
SAVED_OK:
        call    RECORD_SAVED            ;Note the saved file's details
        cmp     CONV_COMMIT, 0          ;Give back the memory used to
        je      SF_DONE                 ; write LF-only lines
        mov     rcx, CONV_BUF
        xor     edx, edx
        mov     r8d, MEM_DECOMMIT
        CALLWIN VirtualFree
        mov     CONV_COMMIT, 0
SF_DONE:
        clc                             ;Saved
        lea     rsp, [rbp]
        pop     rbp
        ret
TEXT_IN_TMP:
        lea     rdi, MSG_BUF            ;"Couldn't save it - your text
        lea     rsi, KEPT_MESS          ; is in name.$$$"
        call    STRCPY_END
        lea     rsi, NAME_TMP
        call    STRCPY_END
        lea     rsi, MSG_BUF
        jmp     SAVE_PROBLEM
BAD_NAME:
        call    BEEP                    ;Sound the bell
        jmp     NEXT_TRY                ;Let the user fix the name
SAVE_PROBLEM:
        call    SHOW_MESSAGE            ;Show the message in RSI, and
        call    BEEP                    ; wait for a key before asking
        call    GET_KEY                 ; for the name again
        mov     PROMPT_MSG, 0
        jmp     NEXT_TRY
SAVE_FILE       ENDP

;-----------------------------------------------------------------------
; Get the bytes to save ready: move the gap to the end so the text is
; in one piece, convert to LF-only lines if the file used them, and
; hash the result.  Sets OUT_PTR, OUT_SIZE, OUT_HASH, OUT_HASHED.
; CF=1 if there isn't memory for the conversion.
;-----------------------------------------------------------------------
PREPARE_OUTPUT  PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rsi, LAST_CHAR          ;Move the gap to the end so the
        call    MOVE_GAP                ; text is all in one piece
        mov     r8, GAP_START
        sub     r8, r15                 ;Number of chars in file
IF ADD_EOF_MARK
        mov     byte ptr [r15+r8], 1AH  ;Add the EOF marker
        inc     r8                      ;Plus one character for EOF
ENDIF
        mov     OUT_PTR, r15
        mov     OUT_SIZE, r8
        cmp     LF_MODE, 0              ;Write LF-only lines?
        je      PO_HASH
        push    r8
        mov     rcx, CONV_BUF
        mov     rdx, r8
        lea     r8, CONV_COMMIT
        call    COMMIT_BUF              ;Memory for the converted copy
        pop     rcx
        jc      PO_RET
        mov     rsi, r15                ;Copy to the conversion buffer,
        mov     rdi, CONV_BUF           ; dropping the CR of each CR/LF
        test    rcx, rcx
        jz      STRIP_DONE
STRIP_LOOP:
        lodsb
        cmp     al, CR                  ;Carriage return?
        jne     STRIP_STORE
        cmp     rcx, 1                  ;Last character in the file?
        je      STRIP_STORE
        cmp     byte ptr [rsi], LF      ;Followed by a line feed?
        je      STRIP_SKIP              ;If yes, leave the CR out
STRIP_STORE:
        stosb
STRIP_SKIP:
        dec     rcx
        jnz     STRIP_LOOP
STRIP_DONE:
        mov     rax, CONV_BUF           ;Write the converted copy
        mov     OUT_PTR, rax
        sub     rdi, rax
        mov     OUT_SIZE, rdi
PO_HASH:
        mov     OUT_HASHED, 0
        mov     rcx, OUT_SIZE
        cmp     rcx, HASH_MAX           ;Small enough to hash?
        ja      PO_OK
        mov     rsi, OUT_PTR
        mov     rax, FNV_BASIS
        call    FNV_UPDATE
        mov     OUT_HASH, rax
        mov     OUT_HASHED, 1
PO_OK:
        clc
PO_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
PREPARE_OUTPUT  ENDP

;-----------------------------------------------------------------------
; Create or open the file named by the UTF-16 string at RCX (EDX = how:
; CREATE_ALWAYS, CREATE_NEW or TRUNCATE_EXISTING) and write OUT_SIZE
; bytes from OUT_PTR to it, 1 GB at a time.  CF=1 on any failure.
;-----------------------------------------------------------------------
WRITE_FILE      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    r12
        push    r13
        mov     r10d, edx               ;How to open it (5th argument)
        mov     edx, GENERIC_WRITE
        mov     r8d, FILE_SHARE_READ
        xor     r9d, r9d
        CALLWIN CreateFileW, r10, FILE_ATTRIBUTE_ARCHIVE, 0
        cmp     rax, INVALID_HANDLE_VALUE
        je      WF_FAIL
        mov     H_FILE, rax
        mov     r12, OUT_PTR            ;Next byte to write
        mov     r13, OUT_SIZE           ;Bytes left to write
WF_LOOP:
        test    r13, r13                ;Entire file written?
        jz      WF_DONE
        mov     r8, r13
        mov     eax, IO_CHUNK
        cmp     r8, rax                 ;Write at most 1 GB at a time
        jbe     WF_CHUNK
        mov     r8, rax
WF_CHUNK:
        mov     rcx, H_FILE
        mov     rdx, r12
        lea     r9, IO_COUNT
        CALLWIN WriteFile, 0            ;Write the buffer contents
        test    eax, eax
        jz      WF_ERROR                ;Exit on a write error
        mov     eax, IO_COUNT
        test    eax, eax                ;Nothing written (disk full)?
        jz      WF_ERROR
        add     r12, rax
        sub     r13, rax
        jmp     WF_LOOP
WF_DONE:
        mov     rcx, H_FILE
        CALLWIN CloseHandle
        pop     r13
        pop     r12
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
WF_ERROR:
        mov     rcx, H_FILE
        CALLWIN CloseHandle
WF_FAIL:
        pop     r13
        pop     r12
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
WRITE_FILE      ENDP

;-----------------------------------------------------------------------
; The "stamp" says which file was loaded or last saved (volume serial
; number and file ID, so the same file reached by another path still
; counts), when it was last written, its size, and for files up to
; HASH_MAX a hash of its contents.  RECORD_LOADED fills it in from the
; open file H_FILE and the R12 bytes just read into CONV_BUF;
; RECORD_SAVED from the file just saved.  CHECK_STAMP (after GET_INFO
; on the file about to be replaced) returns CF=1 if another program
; has changed that file, or replaced it with a new one of the same
; name.
;-----------------------------------------------------------------------
RECORD_LOADED   PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     STAMP_VALID, 0
        mov     rcx, H_FILE
        lea     rdx, BHFI
        CALLWIN GetFileInformationByHandle
        test    eax, eax
        jz      RL_RET
        call    STAMP_FROM_INFO
        call    STAMP_NAME_SET
        mov     STAMP_HASHED, 0
        cmp     r12, HASH_MAX           ;Small enough to hash?
        ja      RL_RET
        mov     rsi, CONV_BUF
        mov     rcx, r12
        mov     rax, FNV_BASIS
        call    FNV_UPDATE
        mov     STAMP_HASH, rax
        mov     STAMP_HASHED, 1
RL_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
RECORD_LOADED   ENDP

RECORD_SAVED    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     STAMP_VALID, 0
        call    GET_INFO
        jc      RS_RET
        call    STAMP_FROM_INFO
        call    STAMP_NAME_SET
        mov     rax, OUT_HASH
        mov     STAMP_HASH, rax
        mov     al, OUT_HASHED
        mov     STAMP_HASHED, al
RS_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
RECORD_SAVED    ENDP

; Copy the identity, time and size from BHFI into the stamp
STAMP_FROM_INFO PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     eax, dword ptr BHFI+28  ;dwVolumeSerialNumber
        mov     STAMP_VOL, eax
        mov     eax, dword ptr BHFI+44  ;nFileIndexHigh
        shl     rax, 32
        mov     ecx, dword ptr BHFI+48  ;nFileIndexLow
        or      rax, rcx
        mov     STAMP_INDEX, rax
        mov     rax, qword ptr BHFI+20  ;ftLastWriteTime
        mov     STAMP_TIME, rax
        mov     eax, dword ptr BHFI+32  ;nFileSizeHigh
        shl     rax, 32
        mov     ecx, dword ptr BHFI+36  ;nFileSizeLow
        or      rax, rcx
        mov     STAMP_SIZE, rax
        mov     STAMP_VALID, 1
        lea     rsp, [rbp]
        pop     rbp
        ret
STAMP_FROM_INFO ENDP

;-----------------------------------------------------------------------
; Fill BHFI for the file named by WNAME.  CF=1 if it can't be opened.
;-----------------------------------------------------------------------
GET_INFO        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     rcx, WNAME
        mov     edx, FILE_READ_ATTRIBUTES
        mov     r8d, FILE_SHARE_READ or FILE_SHARE_WRITE or FILE_SHARE_DELETE
        xor     r9d, r9d
        CALLWIN CreateFileW, OPEN_EXISTING, 0, 0
        cmp     rax, INVALID_HANDLE_VALUE
        je      GI_FAIL
        push    rax
        mov     rcx, rax
        lea     rdx, BHFI
        CALLWIN GetFileInformationByHandle
        pop     rcx
        push    rax
        CALLWIN CloseHandle
        pop     rax
        test    eax, eax
        jz      GI_FAIL
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
GI_FAIL:
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
GET_INFO        ENDP

CHECK_STAMP     PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     STAMP_VALID, 0          ;Anything to compare with?
        je      CS_SAME
        mov     eax, dword ptr BHFI+28  ;The same file?
        cmp     eax, STAMP_VOL
        jne     CS_OTHER
        mov     eax, dword ptr BHFI+44
        shl     rax, 32
        mov     ecx, dword ptr BHFI+48
        or      rax, rcx
        cmp     rax, STAMP_INDEX
        jne     CS_OTHER
        mov     rax, qword ptr BHFI+20
        cmp     rax, STAMP_TIME         ;Written to since?
        jne     CS_CHANGED
        mov     eax, dword ptr BHFI+32
        shl     rax, 32
        mov     ecx, dword ptr BHFI+36
        or      rax, rcx
        cmp     rax, STAMP_SIZE         ;Different size?
        jne     CS_CHANGED
        cmp     STAMP_HASHED, 0         ;Same time and size: compare
        je      CS_SAME                 ; the contents too, as a change
        call    HASH_FILE               ; can fall within the time
        jc      CS_SAME                 ; stamp's resolution
        cmp     rax, STAMP_HASH
        jne     CS_CHANGED
CS_SAME:
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
CS_OTHER:
        call    STAMP_NAME_SAME         ;A different file.  Under the
        je      CS_CHANGED              ; same name, it was replaced
        jmp     CS_SAME                 ; (else this is "Save as")
CS_CHANGED:
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
CHECK_STAMP     ENDP

; Remember the name the stamp belongs to
STAMP_NAME_SET  PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rsi
        push    rdi
        mov     rsi, NAME_POINTER
        lea     rdi, STAMP_NAME
        call    STRCPY_END
        pop     rdi
        pop     rsi
        lea     rsp, [rbp]
        pop     rbp
        ret
STAMP_NAME_SET  ENDP

; ZF=1 if the name being saved to is the stamp's (ignoring the case of
; A-Z, as Windows does)
STAMP_NAME_SAME PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rsi
        push    rdi
        mov     rsi, NAME_POINTER
        lea     rdi, STAMP_NAME
SN_LOOP:
        movzx   eax, byte ptr [rsi]
        movzx   ecx, byte ptr [rdi]
        cmp     eax, "A"
        jb      SN_1
        cmp     eax, "Z"
        ja      SN_1
        or      eax, 20H
SN_1:   cmp     ecx, "A"
        jb      SN_2
        cmp     ecx, "Z"
        ja      SN_2
        or      ecx, 20H
SN_2:   cmp     eax, ecx
        jne     SN_DONE                 ;ZF=0: different
        inc     rsi
        inc     rdi
        test    eax, eax
        jnz     SN_LOOP                 ;ZF=1 at the end: the same
SN_DONE:
        pop     rdi
        pop     rsi
        lea     rsp, [rbp]
        pop     rbp
        ret
STAMP_NAME_SAME ENDP

;-----------------------------------------------------------------------
; RAX = FNV-1a hash of the file named by WNAME, read 1 MB at a time
; into CONV_BUF.  CF=1 if it can't be read.
;-----------------------------------------------------------------------
HASH_FILE       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rcx, CONV_BUF
        mov     edx, 100000H
        lea     r8, CONV_COMMIT
        call    COMMIT_BUF              ;1 MB to read into
        jc      HF_RET
        lea     rcx, WNAME
        mov     edx, GENERIC_READ
        mov     r8d, FILE_SHARE_READ or FILE_SHARE_WRITE or FILE_SHARE_DELETE
        xor     r9d, r9d
        CALLWIN CreateFileW, OPEN_EXISTING, 0, 0
        cmp     rax, INVALID_HANDLE_VALUE
        je      HF_FAIL
        mov     H_FILE, rax
        mov     rax, FNV_BASIS
        mov     HASH_TMP, rax
HF_READ:
        mov     rcx, H_FILE
        mov     rdx, CONV_BUF
        mov     r8d, 100000H
        lea     r9, IO_COUNT
        CALLWIN ReadFile, 0
        test    eax, eax
        jz      HF_ERROR
        mov     ecx, IO_COUNT
        test    ecx, ecx                ;End of file?
        jz      HF_DONE
        mov     rsi, CONV_BUF
        mov     rax, HASH_TMP
        call    FNV_UPDATE
        mov     HASH_TMP, rax
        jmp     HF_READ
HF_DONE:
        mov     rcx, H_FILE
        CALLWIN CloseHandle
        mov     rax, HASH_TMP
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
HF_ERROR:
        mov     rcx, H_FILE
        CALLWIN CloseHandle
HF_FAIL:
        stc
HF_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
HASH_FILE       ENDP

;-----------------------------------------------------------------------
; Add RCX bytes at RSI to the FNV-1a hash in RAX.  Destroys RCX, RDX,
; RSI, R8.
;-----------------------------------------------------------------------
FNV_UPDATE      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        test    rcx, rcx
        jz      FU_RET
        mov     r8, FNV_PRIME
FU_LOOP:
        movzx   edx, byte ptr [rsi]
        xor     rax, rdx
        imul    rax, r8
        inc     rsi
        dec     rcx
        jnz     FU_LOOP
FU_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
FNV_UPDATE      ENDP

;-----------------------------------------------------------------------
; Copy the zero-terminated string at RSI to RDI, leaving RDI at the
; terminating zero (so another string can be added).
;-----------------------------------------------------------------------
STRCPY_END      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
SC_LOOP:
        lodsb
        stosb
        test    al, al
        jnz     SC_LOOP
        dec     rdi
        lea     rsp, [rbp]
        pop     rbp
        ret
STRCPY_END      ENDP

;-----------------------------------------------------------------------
; FIND (Ctrl-F or Alt-F): ask for a string and search forward from the
; cursor, wrapping around at the end of the file.  Letters match either
; case (Latin, Greek and Cyrillic).  Ctrl-R in the prompt switches to a
; regular expression.  FIND_AGAIN (Ctrl-L) repeats the last search.
;-----------------------------------------------------------------------
FIND_CMD        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     rsi, FIND_PROMPTS
        lea     rdi, FIND_BUF           ;Starts with the last string
        mov     r8d, FIND_MAX
        mov     r9d, 3                  ;Any characters; Ctrl-R allowed
        call    EDIT_FIELD
        pushfq
        call    REDO_PROMPT
        popfq
        jnc     TJ_8              ;Search for it
        lea     rsp, [rbp]
        pop     rbp
        ret
TJ_8:
        lea     rsp, [rbp]
        pop     rbp
        jmp     FIND_AGAIN
FIND_CMD        ENDP

FIND_AGAIN      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     byte ptr FIND_BUF, 0    ;Anything to look for?
        je      FIND_NOTHING
        call    COMPILE                 ;Turn it into a pattern
        jc      FIND_BAD
        mov     rsi, CURSOR             ;Start after the cursor
        cmp     rsi, LAST_CHAR
        jae     FIND_WRAP
        call    DECODE
        call    SEARCH
        jnc     FIND_FOUND
        cmp     SEARCH_ABORT, 0
        jne     FIND_COMPLEX
FIND_WRAP:
        mov     rsi, TEXT_BEG           ;Then try from the top
        call    SEARCH
        jnc     FIND_FOUND
        cmp     SEARCH_ABORT, 0
        jne     FIND_COMPLEX
        lea     rsi, NOT_FOUND_MESS
FIND_SAY:
        call    SHOW_MESSAGE
FIND_NOTHING:
        lea     rsp, [rbp]
        pop     rbp
        jmp     BEEP
FIND_BAD:
        lea     rsi, BAD_RE_MESS
        jmp     FIND_SAY
FIND_COMPLEX:
        lea     rsi, COMPLEX_MESS
        jmp     FIND_SAY
FIND_FOUND:
        mov     CURSOR, rsi
        lea     rsp, [rbp]
        pop     rbp
        jmp     GOTO_CURSOR             ;Show the match
FIND_AGAIN      ENDP

;-----------------------------------------------------------------------
; REPLACE (Ctrl-H): ask for a string (or regular expression, Ctrl-R)
; and its replacement, then go through the matches from the cursor to
; the end of the file, asking about each one: Y replaces it, N skips
; it, A replaces it and all the rest, Esc stops.  The replacement is
; plain text.  All the replacements are undone together.
;-----------------------------------------------------------------------
REPLACE_CMD     PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     MARK_MODE, 0            ;End any selection first
        je      RP_NOMARK
        call    MARK
RP_NOMARK:
        lea     rsi, REPL_PROMPTS
        lea     rdi, FIND_BUF
        mov     r8d, FIND_MAX
        mov     r9d, 3
        call    EDIT_FIELD
        jc      RP_ESC
        cmp     byte ptr FIND_BUF, 0
        je      RP_ESC
        call    COMPILE
        jc      RP_BAD
        lea     rsi, WITH_MESS
        lea     rdi, REPL_BUF
        mov     r8d, FIND_MAX
        mov     r9d, 1
        call    EDIT_FIELD
        jc      RP_ESC
        mov     rax, CURSOR
        mov     RP_START, rax
        mov     RP_COUNT, 0
        mov     RP_ALL, 0
        mov     RP_SEEN, 0
RP_LOOP:
        mov     rsi, RP_START
        call    SEARCH                  ;Next match
        jc      RP_DONE
        mov     RP_SEEN, 1
        mov     CURSOR, rsi
        cmp     RP_ALL, 0
        jne     RP_REPLACE
        mov     MARK_START, rsi         ;Show the match highlighted
        mov     MARK_END, rdi
RP_ASK:
        call    SHOW_CURSOR             ;Bring it into view
        call    DISPLAY_SCREEN
        mov     DIRTY_BITS, 0
        movzx   edx, ROWS
        inc     edx
        shl     edx, 16
        lea     rsi, ASK_MESS
        call    TTY_STRING
        call    GET_KEY
        cmp     eax, RESIZE_KEY         ;Window changed: ask again
        je      RP_ASK
        or      al, 20H
        cmp     al, "y"
        je      RP_YES
        cmp     al, "a"
        je      RP_ALLKEY
        cmp     al, "n"
        jne     RP_STOP
        mov     rsi, MARK_END           ;Skip this one
        mov     RP_START, rsi
        mov     rsi, MARK_START
        mov     CURSOR, rsi
        jmp     RP_LOOP
RP_ALLKEY:
        mov     RP_ALL, 1
RP_YES:
        mov     rsi, MARK_START
        mov     rdi, MARK_END
RP_REPLACE:
        mov     MARK_START, -1          ;Nothing highlighted
        mov     MARK_END, -1
        mov     CURSOR, rsi             ;(CURSOR follows the gap moves)
        mov     rcx, rdi
        xor     edi, edi
        call    DELETE_RANGE            ;Out with the match
        lea     rdi, REPL_BUF
        call    STRLEN
        mov     rcx, rax
        mov     rsi, rdi
        call    INSERT_BYTES            ;In with the new text
        jc      RP_STOP
        mov     CURSOR, rax
        mov     RP_START, rax
        inc     RP_COUNT
        jmp     RP_LOOP
RP_STOP:
RP_DONE:
        mov     MARK_START, -1
        mov     MARK_END, -1
        call    SHOW_CURSOR
        mov     DIRTY_BITS, 1
        call    REDO_PROMPT
        cmp     SEARCH_ABORT, 0
        jne     RP_COMPLEX
        cmp     RP_SEEN, 0
        je      RP_NONE
        lea     rdi, MSG_BUF            ;"n replaced"
        mov     rax, RP_COUNT
        call    UTOA
        lea     rsi, REPLACED_MESS
        call    STRCPY_END
        lea     rsi, MSG_BUF
        lea     rsp, [rbp]
        pop     rbp
        jmp     SHOW_MESSAGE
RP_NONE:
        lea     rsi, NOT_FOUND_MESS
        jmp     RP_SAY
RP_COMPLEX:
        lea     rsi, COMPLEX_MESS
        jmp     RP_SAY
RP_BAD:
        lea     rsi, BAD_RE_MESS
RP_SAY:
        call    REDO_PROMPT
        call    SHOW_MESSAGE
        lea     rsp, [rbp]
        pop     rbp
        jmp     BEEP
RP_ESC:
        mov     DIRTY_BITS, 1
        lea     rsp, [rbp]
        pop     rbp
        jmp     REDO_PROMPT
REPLACE_CMD     ENDP

;-----------------------------------------------------------------------
; Make the screen show the cursor: if it is on one of the rows already
; showing, keep the screen where it is, otherwise centre it.
;-----------------------------------------------------------------------
SHOW_CURSOR     PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rax, CURSOR
        mov     rsi, TOP_OF_SCREEN
        cmp     rax, rsi                ;Above the screen?
        jb      SC_CENTRE
        xor     edx, edx                ;Row 0
SC_ROW:
        push    rdx
        call    FIND_NEXT               ;Start of the next row
        pop     rdx
        jc      SC_HERE                 ;Last line of the file
        cmp     CURSOR, rsi             ;Before that?
        jb      SC_HERE
        add     edx, 10000H
        mov     eax, edx
        shr     eax, 16
        cmp     ax, ROWS
        jbe     SC_ROW
SC_CENTRE:
        lea     rsp, [rbp]
        pop     rbp
        jmp     GOTO_CURSOR
SC_HERE:
        mov     rsi, CURSOR
        call    LOCATE                  ;Same rows, new column
        mov     DIRTY_BITS, 1
        lea     rsp, [rbp]
        pop     rbp
        ret
SHOW_CURSOR     ENDP

;-----------------------------------------------------------------------
; Write RAX in decimal at RDI, zero-terminated; RDI is left at the zero.
;-----------------------------------------------------------------------
UTOA            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rcx
        push    rdx
        xor     ecx, ecx
        mov     r8d, 10
UT_DIV:
        xor     edx, edx
        div     r8
        add     dl, "0"
        push    rdx
        inc     ecx
        test    rax, rax
        jnz     UT_DIV
UT_OUT:
        pop     rax
        stosb
        loop    UT_OUT
        mov     byte ptr [rdi], 0
        pop     rdx
        pop     rcx
        lea     rsp, [rbp]
        pop     rbp
        ret
UTOA            ENDP

;-----------------------------------------------------------------------
; Compile FIND_BUF into PAT_NODES: plain text, or (if FIND_REGEX) a
; regular expression with . * + ? ^ $ [...] [^...] \d \w \s \D \W \S
; \t, and \ before any other punctuation to take it literally.  Each
; node is four dwords: type, value, repeat, and for [...] the number
; of ranges in CLASS_ITEMS.  CF=1 if the expression is bad.
;-----------------------------------------------------------------------
COMPILE         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rbx
        lea     rsi, FIND_BUF
        lea     rdi, PAT_NODES
        lea     rbx, CLASS_ITEMS
        mov     C_PREV, 0
CP_LOOP:
        cmp     byte ptr [rsi], 0
        je      CP_END
        call    DECODE_STR              ;EAX = next character
        cmp     FIND_REGEX, 0
        je      CP_LIT
        cmp     eax, "*"
        je      CP_STAR
        cmp     eax, "+"
        je      CP_PLUS
        cmp     eax, "?"
        je      CP_OPT
        mov     edx, T_ANY
        cmp     eax, "."
        je      CP_NODE
        mov     edx, T_BOL
        cmp     eax, "^"
        je      CP_ANCHOR
        mov     edx, T_EOL
        cmp     eax, "$"
        je      CP_ANCHOR
        cmp     eax, "["
        je      CP_CLASS
        cmp     eax, "\"
        jne     CP_LIT
        call    CP_ESCAPE               ;EDX = type, EAX = character
        jc      CP_BAD
        cmp     edx, T_LIT
        jne     CP_NODE
CP_LIT:
        call    FOLD
        mov     edx, T_LIT
CP_NODE:
        mov     C_PREV, rdi             ;A repeat may follow this
CP_PUT:
        mov     [rdi], edx
        mov     [rdi+4], eax
        mov     dword ptr [rdi+8], Q_ONE
        add     rdi, 16
        jmp     CP_LOOP
CP_ANCHOR:
        mov     C_PREV, 0               ;Nothing to repeat
        jmp     CP_PUT
CP_STAR:
        mov     eax, Q_STAR
        jmp     CP_REPEAT
CP_PLUS:
        mov     eax, Q_PLUS
        jmp     CP_REPEAT
CP_OPT:
        mov     eax, Q_OPT
CP_REPEAT:
        mov     rdx, C_PREV             ;Something to repeat?
        test    rdx, rdx
        jz      CP_BAD
        mov     [rdx+8], eax
        mov     C_PREV, 0
        jmp     CP_LOOP
CP_CLASS:
        mov     edx, T_CLASS
        cmp     byte ptr [rsi], "^"
        jne     CP_CL_POS
        mov     edx, T_NCLASS
        inc     rsi
CP_CL_POS:
        mov     C_PREV, rdi
        mov     [rdi], edx
        mov     rax, rbx
        lea     rcx, CLASS_ITEMS
        sub     rax, rcx
        shr     eax, 3
        mov     [rdi+4], eax            ;First range
        mov     dword ptr [rdi+8], Q_ONE
        mov     dword ptr [rdi+12], 0
        cmp     byte ptr [rsi], "]"     ;A ] first is just a character
        jne     CP_CL_LOOP
        inc     rsi
        mov     eax, "]"
        jmp     CP_CL_LO
CP_CL_LOOP:
        cmp     byte ptr [rsi], 0
        je      CP_BAD                  ;No closing ]
        call    DECODE_STR
        cmp     eax, "]"
        je      CP_CL_END
        cmp     eax, "\"
        jne     CP_CL_LO
        call    CP_ESCAPE
        jc      CP_BAD
        cmp     edx, T_LIT
        je      CP_CL_LO
        or      edx, 80000000H          ;\d \w \s etc. inside [...]
        mov     [rbx], edx
        jmp     CP_CL_ITEM
CP_CL_LO:
        mov     [rbx], eax              ;Low end
        mov     [rbx+4], eax
        cmp     byte ptr [rsi], "-"     ;A range?
        jne     CP_CL_ITEM
        cmp     byte ptr [rsi+1], "]"   ;No: a - at the end
        je      CP_CL_ITEM
        cmp     byte ptr [rsi+1], 0
        je      CP_CL_ITEM
        inc     rsi
        call    DECODE_STR              ;High end
        cmp     eax, "\"
        jne     CP_CL_HI
        call    CP_ESCAPE
        jc      CP_BAD
        cmp     edx, T_LIT
        jne     CP_BAD                  ;Can't end a range with \d
CP_CL_HI:
        cmp     eax, [rbx]
        jb      CP_BAD                  ;Backwards range
        mov     [rbx+4], eax
CP_CL_ITEM:
        add     rbx, 8
        inc     dword ptr [rdi+12]
        jmp     CP_CL_LOOP
CP_CL_END:
        add     rdi, 16
        jmp     CP_LOOP
CP_END:
        mov     dword ptr [rdi], T_END
        mov     PAT_FIRST, 0            ;Can the search look for the
        lea     rdi, PAT_NODES          ; first letter quickly?
        cmp     dword ptr [rdi], T_LIT
        jne     CP_OK
        cmp     dword ptr [rdi+8], Q_STAR
        je      CP_OK
        cmp     dword ptr [rdi+8], Q_OPT
        je      CP_OK
        mov     eax, [rdi+4]
        cmp     eax, 80H
        jae     CP_OK
        mov     PAT_FIRST, al
CP_OK:
        pop     rbx
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
CP_BAD:
        pop     rbx
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
COMPILE         ENDP

; The character after a \ at RSI: returns EDX = T_LIT and EAX = the
; character, or EDX = T_DIG ... T_NSPC.  CF=1 if not allowed.
CP_ESCAPE       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     byte ptr [rsi], 0       ;\ at the very end
        je      CPE_BAD
        call    DECODE_STR
        mov     edx, T_DIG
        cmp     eax, "d"
        je      CPE_OK
        mov     edx, T_NDIG
        cmp     eax, "D"
        je      CPE_OK
        mov     edx, T_WRD
        cmp     eax, "w"
        je      CPE_OK
        mov     edx, T_NWRD
        cmp     eax, "W"
        je      CPE_OK
        mov     edx, T_SPC
        cmp     eax, "s"
        je      CPE_OK
        mov     edx, T_NSPC
        cmp     eax, "S"
        je      CPE_OK
        mov     edx, T_LIT
        cmp     eax, "t"
        jne     CPE_1
        mov     eax, TAB
        jmp     CPE_OK
CPE_1:
        cmp     eax, "0"                ;Other letters and digits are
        jb      CPE_OK                  ; kept for the future
        cmp     eax, "9"
        jbe     CPE_BAD
        mov     ecx, eax
        or      ecx, 20H
        cmp     ecx, "a"
        jb      CPE_OK
        cmp     ecx, "z"
        jbe     CPE_BAD
CPE_OK:
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
CPE_BAD:
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
CP_ESCAPE       ENDP

;-----------------------------------------------------------------------
; Search for the compiled pattern starting at RSI.  Returns CF=0 with
; RSI at the start of the match and RDI at its end, or CF=1 if there
; is none before the end of the file (SEARCH_ABORT = 1 if the search
; gave up because the pattern needed too much backtracking).  A match
; never includes a line end, and is never empty.
;-----------------------------------------------------------------------
SEARCH          PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     STEPS, STEP_LIMIT
        mov     SEARCH_ABORT, 0
        lea     rdx, PAT_NODES
        movzx   ecx, PAT_FIRST
S_LOOP:
        cmp     rsi, LAST_CHAR
        jae     S_FAIL
        test    ecx, ecx                ;Quick look for the first letter?
        jz      S_TRY
        movzx   eax, byte ptr [rsi]
        cmp     eax, ecx
        je      S_TRY
        cmp     ecx, "a"                ;Either case of a letter
        jb      S_SKIP
        cmp     ecx, "z"
        ja      S_SKIP
        add     eax, 20H
        cmp     eax, ecx
        je      S_TRY
S_SKIP:
        NEXTP   rsi                     ;(ASCII is never inside a
        jmp     S_LOOP                  ; longer character)
S_TRY:
        mov     MATCH_START, rsi
        call    MATCH
        jnc     S_FOUND
        cmp     SEARCH_ABORT, 0
        jne     S_FAIL
        call    DECODE                  ;On to the next character
        jmp     S_LOOP
S_FAIL:
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
S_FOUND:
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
SEARCH          ENDP

;-----------------------------------------------------------------------
; Does the pattern from node RDX match at RSI?  Returns CF=0 and RDI =
; the end of the match.  The repeats are greedy, backing off one
; character at a time.  RBX, RCX, RDX and RSI are saved; destroys RAX,
; R8-R11.
;-----------------------------------------------------------------------
MATCH           PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rbx
        push    rcx
        push    rdx
        push    rsi
MT_NODE:
        mov     eax, [rdx]
        test    eax, eax                ;End of the pattern?
        jz      MT_END
        cmp     eax, T_BOL
        je      MT_BOL
        cmp     eax, T_EOL
        je      MT_EOL
        mov     ecx, [rdx+8]            ;Repeat
        test    ecx, ecx
        jnz     MT_REPEAT
        call    ONE                     ;Just one character
        jc      MT_FAIL
        add     rdx, 16
        jmp     MT_NODE
MT_BOL:
        cmp     rsi, TEXT_BEG           ;Start of a line?
        je      MT_NEXT
        mov     rax, rsi
        PREVP   rax
        cmp     byte ptr [rax], LF
        je      MT_NEXT
        cmp     byte ptr [rax], CR
        je      MT_NEXT
        jmp     MT_FAIL
MT_EOL:
        cmp     rsi, LAST_CHAR          ;End of a line?
        jae     MT_NEXT
        cmp     byte ptr [rsi], CR
        je      MT_NEXT
        cmp     byte ptr [rsi], LF
        jne     MT_FAIL
MT_NEXT:
        add     rdx, 16
        jmp     MT_NODE
MT_END:
        cmp     rsi, MATCH_START        ;Empty matches don't count
        je      MT_FAIL
        mov     rdi, rsi
        jmp     MT_OK
MT_REPEAT:
        cmp     ecx, Q_OPT
        jne     MT_MANY
        mov     rbx, rsi
        call    ONE                     ;With the character...
        jc      MT_OPT_0
        add     rdx, 16
        call    MATCH
        jnc     MT_OK
        call    MT_STEP
        jc      MT_FAIL
        mov     rsi, rbx                ;...or without it
        jmp     MT_NODE
MT_OPT_0:
        add     rdx, 16
        jmp     MT_NODE
MT_MANY:
        xor     ebx, ebx                ;RBX = how many
MT_GREEDY:
        call    ONE                     ;Take as many as we can
        jc      MT_MAXED
        inc     rbx
        jmp     MT_GREEDY
MT_MAXED:
        xor     ecx, ecx                ;RCX = the fewest allowed
        cmp     dword ptr [rdx+8], Q_PLUS
        jne     MT_TRY
        inc     ecx
        test    rbx, rbx
        jz      MT_FAIL
MT_TRY:
        add     rdx, 16
MT_TRY_LOOP:
        call    MATCH                   ;Does the rest match from here?
        jnc     MT_OK
        cmp     rbx, rcx                ;Can we give one back?
        jbe     MT_FAIL
        call    MT_STEP
        jc      MT_FAIL
        dec     rbx
        call    PREV_CHAR
        jmp     MT_TRY_LOOP
MT_OK:
        pop     rsi
        pop     rdx
        pop     rcx
        pop     rbx
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
MT_FAIL:
        pop     rsi
        pop     rdx
        pop     rcx
        pop     rbx
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
MATCH           ENDP

; Count one backtracking step.  CF=1 (and SEARCH_ABORT set) when the
; search has taken too many.
MT_STEP         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     SEARCH_ABORT, 0
        jne     MS_STOP
        dec     STEPS
        jnz     MS_OK
        mov     SEARCH_ABORT, 1
MS_STOP:
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
MS_OK:
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
MT_STEP         ENDP

;-----------------------------------------------------------------------
; Does pattern node RDX match the character at RSI?  If so, CF=0 and
; RSI moves past it; if not, CF=1 and RSI is unchanged.  Line ends
; never match.  Destroys RAX, R8-R11.
;-----------------------------------------------------------------------
ONE             PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     rsi, LAST_CHAR
        jae     ONE_NO
        cmp     byte ptr [rsi], CR
        je      ONE_NO
        cmp     byte ptr [rsi], LF
        je      ONE_NO
        mov     r8, rsi
        call    DECODE                  ;EAX = the character
        mov     r9d, [rdx]
        cmp     r9d, T_LIT
        jne     ONE_2
        call    FOLD
        cmp     eax, [rdx+4]
        je      ONE_YES
        jmp     ONE_BACK
ONE_2:
        cmp     r9d, T_ANY
        je      ONE_YES
        cmp     r9d, T_NCLASS
        ja      ONE_BUILTIN
        call    IN_CLASS                ;CF=0 if in the [...]
        jc      ONE_NOT_IN
        cmp     r9d, T_CLASS
        je      ONE_YES
        jmp     ONE_BACK
ONE_NOT_IN:
        cmp     r9d, T_NCLASS
        je      ONE_YES
        jmp     ONE_BACK
ONE_BUILTIN:
        call    BUILTIN
        jnc     ONE_YES
ONE_BACK:
        mov     rsi, r8
ONE_NO:
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
ONE_YES:
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
ONE             ENDP

; Is code point EAX in the [...] of node RDX (in either case)?  CF=0 if
; so.  Saves all registers but R10 and R11.
IN_CLASS        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rax
        push    rcx
        push    rdx
        push    r9
        mov     r10d, eax               ;R10 = the character
        call    IC_TEST                 ;As it is,
        jnc     IC_RET
        mov     eax, r10d
        call    FOLD                    ; in lower case,
        call    IC_TEST
        jnc     IC_RET
        mov     eax, r10d
        call    UPCASE                  ; and in upper case
        call    IC_TEST
IC_RET:
        pop     r9
        pop     rdx
        pop     rcx
        pop     rax
        lea     rsp, [rbp]
        pop     rbp
        ret
IN_CLASS        ENDP

; Is EAX in the ranges of node RDX?  CF=0 if so.  Destroys RCX, R9, R11.
IC_TEST         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     ecx, [rdx+12]           ;Number of ranges
        mov     r11d, [rdx+4]
        lea     r9, CLASS_ITEMS
        lea     r9, [r9+r11*8]
IT_LOOP:
        test    ecx, ecx
        jz      IT_NO
        mov     r11d, [r9]
        test    r11d, 80000000H         ;\d \w \s and the like
        jnz     IT_BUILTIN
        cmp     eax, r11d
        jb      IT_NEXT
        cmp     eax, [r9+4]
        jbe     IT_YES
IT_NEXT:
        add     r9, 8
        dec     ecx
        jmp     IT_LOOP
IT_BUILTIN:
        push    r9
        and     r11d, 0FFH
        mov     r9d, r11d
        call    BUILTIN
        pop     r9
        jnc     IT_YES
        jmp     IT_NEXT
IT_NO:
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
IT_YES:
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
IC_TEST         ENDP

; Does EAX match \d \D \w \W \s or \S (node type R9D)?  CF=0 if so.
; Saves all registers but R11.
BUILTIN         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rcx
        push    rdx
        mov     ecx, r9d
        sub     ecx, T_DIG              ;0-5
        xor     r11d, r11d              ;R11 = 1 if it is one
        cmp     ecx, 2
        jae     BI_WORD
        cmp     eax, "0"                ;\d: ASCII digits
        jb      BI_GOT
        cmp     eax, "9"
        ja      BI_GOT
        inc     r11d
        jmp     BI_GOT
BI_WORD:
        cmp     ecx, 4
        jae     BI_SPACE
        cmp     eax, "_"
        je      BI_IS
        cmp     eax, "0"
        jb      BI_GOT
        cmp     eax, "9"
        jbe     BI_IS
        mov     edx, eax
        or      edx, 20H
        cmp     edx, "a"
        jb      BI_GOT
        cmp     edx, "z"
        jbe     BI_IS
        cmp     eax, 80H
        jb      BI_GOT
        lea     rdx, WORD_TABLE
        push    rcx
        mov     ecx, WORD_COUNT
        jmp     BI_TABLE
BI_SPACE:
        cmp     eax, " "
        je      BI_IS
        cmp     eax, 9
        jb      BI_GOT
        cmp     eax, 0DH
        jbe     BI_IS
        cmp     eax, 1CH
        jb      BI_GOT
        cmp     eax, 1FH
        jbe     BI_IS
        cmp     eax, 80H
        jb      BI_GOT
        lea     rdx, SPACE_TABLE
        push    rcx
        mov     ecx, SPACE_COUNT
BI_TABLE:
        cmp     eax, [rdx]              ;Sorted ranges
        jb      BI_TDONE
        cmp     eax, [rdx+4]
        jbe     BI_TYES
        add     rdx, 8
        dec     ecx
        jnz     BI_TABLE
        jmp     BI_TDONE
BI_TYES:
        inc     r11d
BI_TDONE:
        pop     rcx
        jmp     BI_GOT
BI_IS:
        inc     r11d
BI_GOT:
        and     ecx, 1                  ;\D \W \S: the opposite
        xor     r11d, ecx
        pop     rdx
        pop     rcx
        cmp     r11d, 1                 ;CF=0 if R11 = 1
        lea     rsp, [rbp]
        pop     rbp
        ret
BUILTIN         ENDP

;-----------------------------------------------------------------------
; FOLD: lower case of code point EAX (A-Z, Latin-1, Latin Extended-A,
; Greek, Cyrillic).  UPCASE: the upper case.  Other registers saved.
;-----------------------------------------------------------------------
FOLD            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     eax, "A"
        jb      FO_RET
        cmp     eax, "Z"
        jbe     FO_20
        cmp     eax, 0C0H
        jb      FO_RET
        cmp     eax, 0DEH
        ja      FO_EXTA
        cmp     eax, 0D7H               ;Multiplication sign
        je      FO_RET
FO_20:  add     eax, 20H
FO_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
FO_EXTA:
        cmp     eax, 100H
        jb      FO_RET
        cmp     eax, 17FH
        ja      FO_GREEK
        cmp     eax, 178H               ;Y with diaeresis
        jne     FO_PAIRS
        mov     eax, 0FFH
        lea     rsp, [rbp]
        pop     rbp
        ret
FO_PAIRS:
        cmp     eax, 138H               ;100-137: upper case even
        jb      FO_EVEN
        je      FO_RET
        cmp     eax, 149H               ;139-148: upper case odd
        jb      FO_ODD
        je      FO_RET
        cmp     eax, 178H               ;14A-177: even
        jb      FO_EVEN
        cmp     eax, 17FH               ;179-17E: odd
        jb      FO_ODD
        lea     rsp, [rbp]
        pop     rbp
        ret
FO_EVEN:
        test    al, 1
        jnz     FO_RET
        inc     eax
        lea     rsp, [rbp]
        pop     rbp
        ret
FO_ODD:
        test    al, 1
        jz      FO_RET
        inc     eax
        lea     rsp, [rbp]
        pop     rbp
        ret
FO_GREEK:
        cmp     eax, 386H
        jb      FO_RET
        jne     FO_G1
        mov     eax, 3ACH
        lea     rsp, [rbp]
        pop     rbp
        ret
FO_G1:  cmp     eax, 388H
        jb      FO_RET
        cmp     eax, 38AH
        ja      FO_G2
        add     eax, 25H
        lea     rsp, [rbp]
        pop     rbp
        ret
FO_G2:  cmp     eax, 38CH
        jne     FO_G3
        mov     eax, 3CCH
        lea     rsp, [rbp]
        pop     rbp
        ret
FO_G3:  cmp     eax, 38EH
        jb      FO_RET
        cmp     eax, 38FH
        ja      FO_G4
        add     eax, 3FH
        lea     rsp, [rbp]
        pop     rbp
        ret
FO_G4:  cmp     eax, 391H
        jb      FO_RET
        cmp     eax, 3A9H
        ja      FO_G5
        cmp     eax, 3A2H
        je      FO_RET
        jmp     FO_20
FO_G5:  cmp     eax, 3C2H               ;Final sigma
        jne     FO_CYR
        inc     eax
        lea     rsp, [rbp]
        pop     rbp
        ret
FO_CYR: cmp     eax, 400H
        jb      FO_RET
        cmp     eax, 40FH
        ja      FO_C1
        add     eax, 50H
        lea     rsp, [rbp]
        pop     rbp
        ret
FO_C1:  cmp     eax, 42FH
        jbe     FO_20
        lea     rsp, [rbp]
        pop     rbp
        ret
FOLD            ENDP

UPCASE          PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     eax, "a"
        jb      UP_RET
        cmp     eax, "z"
        jbe     UP_20
        cmp     eax, 0E0H
        jb      UP_RET
        cmp     eax, 0FEH
        ja      UP_FF
        cmp     eax, 0F7H               ;Division sign
        je      UP_RET
UP_20:  sub     eax, 20H
UP_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
UP_FF:  cmp     eax, 0FFH
        jne     UP_EXTA
        mov     eax, 178H
        lea     rsp, [rbp]
        pop     rbp
        ret
UP_EXTA:
        cmp     eax, 100H
        jb      UP_RET
        cmp     eax, 17FH
        ja      UP_GREEK
        cmp     eax, 138H               ;101-137: lower case odd
        jb      UP_ODD
        je      UP_RET
        cmp     eax, 149H               ;13A-148: even
        jb      UP_EVEN
        je      UP_RET
        cmp     eax, 178H               ;14B-177: odd
        jb      UP_ODD
        je      UP_RET
        cmp     eax, 17FH               ;17A-17E: even
        jb      UP_EVEN
        lea     rsp, [rbp]
        pop     rbp
        ret
UP_ODD: test    al, 1
        jz      UP_RET
        dec     eax
        lea     rsp, [rbp]
        pop     rbp
        ret
UP_EVEN:
        test    al, 1
        jnz     UP_RET
        dec     eax
        lea     rsp, [rbp]
        pop     rbp
        ret
UP_GREEK:
        cmp     eax, 3ACH
        jb      UP_RET
        jne     UP_G1
        mov     eax, 386H
        lea     rsp, [rbp]
        pop     rbp
        ret
UP_G1:  cmp     eax, 3AFH
        ja      UP_G2
        sub     eax, 25H
        lea     rsp, [rbp]
        pop     rbp
        ret
UP_G2:  cmp     eax, 3B1H
        jb      UP_RET
        cmp     eax, 3C9H
        ja      UP_G3
        cmp     eax, 3C2H               ;Final sigma
        jne     UP_20
        mov     eax, 3A3H
        lea     rsp, [rbp]
        pop     rbp
        ret
UP_G3:  cmp     eax, 3CCH
        jb      UP_RET
        jne     UP_G4
        mov     eax, 38CH
        lea     rsp, [rbp]
        pop     rbp
        ret
UP_G4:  cmp     eax, 3CEH
        ja      UP_CYR
        sub     eax, 3FH
        lea     rsp, [rbp]
        pop     rbp
        ret
UP_CYR: cmp     eax, 430H
        jb      UP_RET
        cmp     eax, 44FH
        jbe     UP_20
        cmp     eax, 45FH
        ja      UP_RET
        sub     eax, 50H
        lea     rsp, [rbp]
        pop     rbp
        ret
UPCASE          ENDP

;-----------------------------------------------------------------------
; GO TO LINE (Ctrl-G)
;-----------------------------------------------------------------------
GOTO_LINE       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     byte ptr NUM_BUF, 0
        lea     rsi, LINE_MESS
        lea     rdi, NUM_BUF
        mov     r8d, 12
        xor     r9d, r9d
        call    EDIT_FIELD
        pushfq
        call    REDO_PROMPT
        popfq
        jc      GL_RET                  ;Escape - forget it
        lea     rsi, NUM_BUF            ;Convert the number
        xor     ecx, ecx
GL_DIGIT:
        movzx   eax, byte ptr [rsi]
        sub     eax, "0"
        cmp     eax, 9
        ja      GL_NUMBER
        imul    rcx, rcx, 10
        add     rcx, rax
        inc     rsi
        jmp     GL_DIGIT
GL_NUMBER:
        test    rcx, rcx                ;Line 0 or no number?
        jz      GL_BAD
        dec     rcx
        mov     rsi, TEXT_BEG
GL_LOOP:
        test    rcx, rcx
        jz      GL_THERE
        call    FIND_NEXT               ;Next line (keeps RCX)
        jc      GL_LAST                 ;Past the end - use last line
        dec     rcx
        jmp     GL_LOOP
GL_LAST:
        mov     CURSOR, rsi
        call    FIND_START
GL_THERE:
        mov     CURSOR, rsi
        lea     rsp, [rbp]
        pop     rbp
        jmp     GOTO_CURSOR
GL_BAD:
        call    BEEP
GL_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
GOTO_LINE       ENDP

;-----------------------------------------------------------------------
; Bring the cursor into view: put its line in the middle of the screen
; and scroll sideways if it is past the right edge.
;-----------------------------------------------------------------------
GOTO_CURSOR     PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     LEFT_MARGIN, 0
        call    CURSOR_COL              ;Sets LINE_COL
        mov     eax, LINE_COL
        movzx   ecx, COLUMNS
        cmp     eax, ecx                ;Fits without scrolling?
        jb      GC_ROW
        shr     ecx, 1
        sub     eax, ecx                ;Put it mid-screen
        and     eax, -8
        mov     LEFT_MARGIN, eax
GC_ROW:
        movzx   edx, ROWS
        shr     edx, 1
        shl     edx, 16
        mov     rsi, CURSOR
        call    LOCATE
        mov     DIRTY_BITS, 1
        lea     rsp, [rbp]
        pop     rbp
        ret
GOTO_CURSOR     ENDP

;-----------------------------------------------------------------------
; Show a message on the prompt line until the next key.
;-----------------------------------------------------------------------
SHOW_MESSAGE    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        movzx   edx, ROWS
        inc     edx
        shl     edx, 16
        call    TTY_STRING
        mov     PROMPT_MSG, 1
        lea     rsp, [rbp]
        pop     rbp
        ret
SHOW_MESSAGE    ENDP

;-----------------------------------------------------------------------
; Let the user edit a zero-terminated string on the prompt line.
; RSI = prompt, RDI = buffer, R8 = maximum bytes, R9D bit 0 = allow
; non-ASCII characters (stored as UTF-8), bit 1 = Ctrl-R switches
; FIND_REGEX, and RSI is then a table of two prompts (plain, regex).
; Returns CF=1 for Escape, CF=0 for Enter.
;-----------------------------------------------------------------------
EDIT_FIELD      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     EF_PROMPT, rsi
        mov     EF_BUF, rdi
        mov     EF_MAX, r8
        mov     EF_UNI, r9d
EF_LOOP:
        movzx   edx, ROWS
        inc     edx
        shl     edx, 16                 ;Last row, column 0
        mov     rsi, EF_PROMPT
        test    EF_UNI, 2               ;Prompt depends on the mode?
        jz      EF_SHOWP
        movzx   eax, FIND_REGEX
        mov     rsi, [rsi+rax*8]
EF_SHOWP:
        call    TTY_STRING              ;Display the prompt
        mov     rsi, EF_BUF
        call    TTY_STRING              ;And the text after it
        call    GET_KEY
        cmp     KEY_CHAR, 80H           ;Non-ASCII character?
        jae     EF_WIDE
        or      al, al                  ;Ignore special keys
        jz      EF_LOOP
        cmp     al, 27                  ;Escape?
        je      EF_ESC
        cmp     al, 13                  ;Enter?
        je      EF_ENTER
        cmp     ah, 0EH                 ;Backspace?
        je      EF_BACK
        cmp     al, 18                  ;Ctrl-R: regex on or off
        jne     EF_NOT_R
        test    EF_UNI, 2
        jz      EF_LOOP
        xor     FIND_REGEX, 1
        jmp     EF_LOOP
EF_NOT_R:
        cmp     al, TAB                 ;A tab may be searched for
        je      EF_TAB
        cmp     al, " "                 ;Ignore other control keys
        jb      EF_LOOP
EF_TAB:
        mov     KEY_BYTES, al
        mov     ecx, 1
        jmp     EF_ADD
EF_WIDE:
        test    EF_UNI, 1
        jz      EF_REFUSE
        mov     eax, KEY_CHAR
        lea     rdi, KEY_BYTES
        call    ENCODE_UTF8
EF_ADD:
        mov     rdi, EF_BUF
        call    STRLEN
        lea     rdx, [rax+rcx]
        cmp     rdx, EF_MAX             ;Room for it?
        ja      EF_REFUSE
        add     rdi, rax
        lea     rsi, KEY_BYTES
        rep     movsb
        mov     byte ptr [rdi], 0
        jmp     EF_LOOP
EF_REFUSE:
        call    BEEP
        jmp     EF_LOOP
EF_BACK:
        mov     rdi, EF_BUF
        call    STRLEN
        test    rax, rax
        jz      EF_LOOP
        add     rdi, rax
EF_BACK1:
        dec     rdi                     ;Remove a whole UTF-8 character
        mov     al, [rdi]
        and     al, 0C0H
        cmp     al, 80H
        jne     EF_BACK2
        cmp     rdi, EF_BUF
        ja      EF_BACK1
EF_BACK2:
        mov     byte ptr [rdi], 0
        jmp     EF_LOOP
EF_ESC:
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
EF_ENTER:
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
EDIT_FIELD      ENDP

;-----------------------------------------------------------------------
; Copy the next argument from the UTF-16 command line at RSI into
; ARG_BUF as UTF-8, following the usual rules for quotes.  RSI is left
; after it.  CF=1 if there are no more arguments.
;-----------------------------------------------------------------------
NEXT_ARG        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
NA_SKIP:
        movzx   eax, word ptr [rsi]
        cmp     eax, " "                ;Skip spaces and tabs
        je      NA_SKIP1
        cmp     eax, TAB
        jne     NA_START
NA_SKIP1:
        add     rsi, 2
        jmp     NA_SKIP
NA_START:
        test    eax, eax
        jz      NA_NONE
        lea     rdi, ARG_BUF
        lea     r8, ARG_BUF + MAX_NAME * 3 - 8  ;Room left
        xor     edx, edx                ;EDX = 1 inside quotes
NA_CHAR:
        movzx   eax, word ptr [rsi]
        test    eax, eax
        jz      NA_END
        cmp     eax, '"'                ;Quotes switch quoting on/off
        jne     NA_NOT_QUOTE
        xor     edx, 1
        add     rsi, 2
        jmp     NA_CHAR
NA_NOT_QUOTE:
        test    edx, edx
        jnz     NA_TAKE
        cmp     eax, " "                ;Unquoted space or tab ends it
        je      NA_END
        cmp     eax, TAB
        je      NA_END
NA_TAKE:
        add     rsi, 2
        mov     ecx, eax
        and     ecx, 0FC00H
        cmp     ecx, 0D800H             ;Surrogate pair?
        jne     NA_ENCODE
        movzx   ecx, word ptr [rsi]
        mov     r9d, ecx
        and     r9d, 0FC00H
        cmp     r9d, 0DC00H
        jne     NA_ENCODE
        add     rsi, 2
        sub     eax, 0D800H
        shl     eax, 10
        sub     ecx, 0DC00H
        add     eax, ecx
        add     eax, 10000H
NA_ENCODE:
        cmp     rdi, r8                 ;Ignore anything too long
        jae     NA_CHAR
        call    ENCODE_UTF8
        add     rdi, rcx
        jmp     NA_CHAR
NA_END:
        mov     byte ptr [rdi], 0
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
NA_NONE:
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
NEXT_ARG        ENDP

;-----------------------------------------------------------------------
; Convert the zero-terminated UTF-8 string at RSI to UTF-16 at RDI.
; Destroys RAX, RSI, RDI, R9-R11.
;-----------------------------------------------------------------------
TO_WIDE         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
TW_LOOP:
        cmp     byte ptr [rsi], 0
        je      TW_END
        call    DECODE_STR
        cmp     eax, 10000H             ;Needs a surrogate pair?
        jb      TW_ONE
        sub     eax, 10000H
        mov     r9d, eax
        shr     r9d, 10
        add     r9d, 0D800H
        mov     word ptr [rdi], r9w
        and     eax, 3FFH
        add     eax, 0DC00H
        add     rdi, 2
TW_ONE:
        mov     word ptr [rdi], ax
        add     rdi, 2
        jmp     TW_LOOP
TW_END:
        mov     word ptr [rdi], 0
        lea     rsp, [rbp]
        pop     rbp
        ret
TO_WIDE         ENDP

;-----------------------------------------------------------------------
; RAX = length of the zero-terminated string at RDI.
;-----------------------------------------------------------------------
STRLEN          PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        xor     eax, eax
SL_LOOP:
        cmp     byte ptr [rdi+rax], 0
        je      SL_RET
        inc     rax
        jmp     SL_LOOP
SL_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
STRLEN          ENDP

;=======================================================================
; Gap buffer.  The text is [R15, GAP_START) followed by
; [GAP_END, LAST_CHAR).  Inserts and deletes move the gap to the edit
; point first, so only the text between the old and new edit points
; is moved.
;=======================================================================

;-----------------------------------------------------------------------
; Move the gap so it starts at the position in RSI.  Returns RSI =
; that position (now GAP_END).  Stored positions are adjusted.
; Destroys RAX, RCX, RDI, R8-R11.
;-----------------------------------------------------------------------
MOVE_GAP        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     rsi, GAP_END            ;Already there?
        je      MG_RET
        push    rdx
        mov     rdx, GAP_END
        sub     rdx, GAP_START          ;RDX = gap length
        jnz     MG_HAVE_GAP
        mov     GAP_START, rsi          ;An empty gap can go anywhere
        mov     GAP_END, rsi
        jmp     MG_FIN
MG_HAVE_GAP:
        cmp     rsi, GAP_START
        jae     MG_UP
; Target is before the gap: slide [RSI, GAP_START) up past the gap
        mov     r8, rsi
        mov     r9, GAP_START
        mov     r10, rdx
        call    FIX_RANGE               ;Those positions move up
        mov     rcx, GAP_START
        sub     rcx, rsi
        push    rsi
        lea     rdi, [rsi+rdx]
        call    MEMMOVE
        pop     rsi
        mov     GAP_START, rsi
        add     rsi, rdx
        mov     GAP_END, rsi
        jmp     MG_FIN
; Target is after the gap: slide [GAP_END, RSI) down before the gap
MG_UP:
        mov     r8, GAP_END
        mov     r9, rsi
        mov     r10, rdx
        neg     r10
        call    FIX_RANGE               ;Those positions move down
        mov     rcx, rsi
        sub     rcx, GAP_END
        push    rsi
        mov     rdi, GAP_START
        mov     rsi, GAP_END
        call    MEMMOVE
        pop     rsi
        mov     GAP_END, rsi
        mov     rax, rsi
        sub     rax, rdx
        mov     GAP_START, rax
MG_FIN:
        call    UPDATE_BEG
        pop     rdx
MG_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
MOVE_GAP        ENDP

;-----------------------------------------------------------------------
; Insert RCX bytes from RSI at the cursor.  Afterwards CURSOR (and any
; position that was at the cursor) points at the first inserted byte,
; and RAX = the position just after the inserted text.  CF=1 (with a
; beep) if there isn't room.
;-----------------------------------------------------------------------
INSERT_BYTES    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        test    rcx, rcx
        jnz     IB_SOME
        mov     rax, CURSOR
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
IB_SOME:
        mov     rax, LAST_CHAR          ;Logical size of the text
        sub     rax, r15
        sub     rax, GAP_END
        add     rax, GAP_START
        add     rax, rcx
        mov     r8, FILE_MAX
        cmp     rax, r8                 ;Would it be too big?
        ja      IB_FAIL
        push    rsi
        push    rcx
        mov     rsi, CURSOR
        call    MOVE_GAP                ;Gap to the cursor
        mov     rcx, [rsp]
        mov     rax, GAP_END
        sub     rax, GAP_START
        cmp     rax, rcx                ;Big enough?
        jae     IB_ROOM
        call    GROW_GAP
        jc      IB_FAIL2
IB_ROOM:
        mov     r8, GAP_END             ;Positions at the insertion
        lea     r9, [r8+1]              ; point now refer to the first
        mov     r10, GAP_START          ; new byte
        call    FIX_SET
        pop     rcx
        pop     rsi
        mov     rdi, GAP_START
        mov     rdx, rdi                ;Offset of the new text
        sub     rdx, r15
        push    rcx
        rep     movsb                   ;Fill the start of the gap
        pop     rcx
        mov     GAP_START, rdi
        call    UPDATE_BEG
        mov     eax, REC_INS
        call    LOG_EDIT                ;Note it for UNDO
        mov     rax, GAP_END
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
IB_FAIL2:
        pop     rcx
        pop     rsi
IB_FAIL:
        call    BEEP                    ;Tell the user it didn't fit
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
INSERT_BYTES    ENDP

;-----------------------------------------------------------------------
; Make the gap (now at the cursor) at least RCX bytes long by moving
; the text after it up.  It grows by an extra 1/8 of the text so this
; happens rarely.  CF=1 if the memory isn't available.
;-----------------------------------------------------------------------
GROW_GAP        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rax, LAST_CHAR          ;Logical size of the text
        sub     rax, r15
        sub     rax, GAP_END
        add     rax, GAP_START
        shr     rax, 3
        add     rax, GAP_MIN
        add     rax, rcx                ;RAX = amount to grow by
        mov     rdx, EDIT_COMMIT+8      ;Room left in the reservation:
        sub     rdx, 2 * SLACK          ; reserved size, less slack,
        add     rdx, r15                ; less what is in use
        sub     rdx, LAST_CHAR
        jb      GG_FAIL                 ;None at all
        cmp     rax, rdx
        jbe     GG_SIZE_OK
        mov     rax, rdx
GG_SIZE_OK:
        mov     r9, GAP_END
        sub     r9, GAP_START
        add     r9, rax
        cmp     r9, rcx                 ;Still not enough?
        jb      GG_FAIL
        push    rax
        mov     rcx, r15
        mov     rdx, LAST_CHAR
        add     rdx, rax
        sub     rdx, r15
        lea     r8, EDIT_COMMIT
        call    COMMIT_BUF              ;Get the memory
        pop     rax
        jc      GG_FAIL
        push    rax
        mov     r8, GAP_END             ;Positions after the gap,
        mov     r9, LAST_CHAR           ; including the end of the
        inc     r9                      ; text, move up
        mov     r10, rax
        call    FIX_RANGE
        mov     rax, [rsp]
        mov     rsi, GAP_END
        mov     rcx, LAST_CHAR
        sub     rcx, rsi
        lea     rdi, [rsi+rax]
        call    MEMMOVE
        pop     rax
        add     GAP_END, rax
        add     LAST_CHAR, rax
        call    UPDATE_BEG
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
GG_FAIL:
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
GROW_GAP        ENDP

;-----------------------------------------------------------------------
; Delete the text from RSI up to RCX.  If RDI isn't zero, the deleted
; bytes are copied there first.  Positions inside the range end up at
; the deletion point.
;-----------------------------------------------------------------------
DELETE_RANGE    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        test    rdi, rdi
        jz      DR_NOCOPY
        push    rsi
        push    rcx
        call    COPY_OUT
        pop     rcx
        pop     rsi
DR_NOCOPY:
        call    DIST                    ;RCX = number of bytes
        test    rcx, rcx
        jz      DR_RET
        push    rcx
        call    MOVE_GAP                ;The range now follows the gap
        pop     rcx
        mov     rsi, GAP_END            ;Note it (and its bytes) for UNDO
        mov     rdx, GAP_START
        sub     rdx, r15
        mov     eax, REC_DEL
        call    LOG_EDIT
        mov     r8, GAP_END
        lea     r9, [r8+rcx]
        mov     r10, r9
        call    FIX_SET                 ;Positions in it -> after it
        mov     GAP_END, r9             ;Make the gap swallow it
        call    UPDATE_BEG
DR_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
DELETE_RANGE    ENDP

;-----------------------------------------------------------------------
; Copy the text from RSI up to RCX to RDI.  Destroys RAX, RCX, RSI, RDI.
;-----------------------------------------------------------------------
COPY_OUT        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     rsi, GAP_START
        jae     CO_TAIL
        mov     rax, rcx                ;Part before the gap
        cmp     rax, GAP_END
        jb      CO_FIRST
        mov     rax, GAP_START
CO_FIRST:
        push    rcx
        mov     rcx, rax
        sub     rcx, rsi
        rep     movsb
        pop     rcx
        cmp     rcx, GAP_END
        jb      CO_DONE
        mov     rsi, GAP_END            ;Part after the gap
CO_TAIL:
        sub     rcx, rsi
        rep     movsb
CO_DONE:
        lea     rsp, [rbp]
        pop     rbp
        ret
COPY_OUT        ENDP

;-----------------------------------------------------------------------
; RCX = number of bytes of text from RSI up to RCX.  Destroys RAX.
;-----------------------------------------------------------------------
DIST            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        sub     rcx, rsi
        cmp     rsi, GAP_START          ;Does the range span the gap?
        jae     DIST_RET
        lea     rax, [rsi+rcx]
        cmp     rax, GAP_END
        jb      DIST_RET
        mov     rax, GAP_END
        sub     rax, GAP_START
        sub     rcx, rax                ;Don't count the gap
DIST_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
DIST            ENDP

;-----------------------------------------------------------------------
; R8 = the position RCX bytes after RSI.  Destroys nothing else.
;-----------------------------------------------------------------------
ADVANCE         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     r8, [rsi+rcx]
        cmp     rsi, GAP_START
        jae     ADV_RET
        cmp     r8, GAP_START
        jb      ADV_RET
        add     r8, GAP_END             ;Skip over the gap
        sub     r8, GAP_START
ADV_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
ADVANCE         ENDP

;-----------------------------------------------------------------------
; Add R10 to every stored position in [R8, R9).  FIX_SET instead sets
; them to R10.  Destroy RAX, RCX, R11.
;-----------------------------------------------------------------------
FIX_RANGE       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     r11, PTR_VARS
        mov     ecx, NUM_PTRS
FR_LOOP:
        mov     rax, [r11]
        cmp     rax, r8
        jb      FR_NEXT
        cmp     rax, r9
        jae     FR_NEXT
        add     rax, r10
        mov     [r11], rax
FR_NEXT:
        add     r11, 8
        dec     ecx
        jnz     FR_LOOP
        lea     rsp, [rbp]
        pop     rbp
        ret
FIX_RANGE       ENDP

FIX_SET         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     r11, PTR_VARS
        mov     ecx, NUM_PTRS
FS_LOOP:
        mov     rax, [r11]
        cmp     rax, r8
        jb      FS_NEXT
        cmp     rax, r9
        jae     FS_NEXT
        mov     [r11], r10
FS_NEXT:
        add     r11, 8
        dec     ecx
        jnz     FS_LOOP
        lea     rsp, [rbp]
        pop     rbp
        ret
FIX_SET         ENDP

;-----------------------------------------------------------------------
; TEXT_BEG = position of the first character (GAP_END if the gap is at
; the very start).  Destroys RAX.
;-----------------------------------------------------------------------
UPDATE_BEG      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rax, r15
        cmp     GAP_START, r15
        jne     UB_SET
        mov     rax, GAP_END
UB_SET:
        mov     TEXT_BEG, rax
        lea     rsp, [rbp]
        pop     rbp
        ret
UPDATE_BEG      ENDP


;-----------------------------------------------------------------------
; Copy RCX bytes from RSI to RDI; the areas may overlap.  Backward
; copies use qwords because REP MOVSB is slow going backward.
; Destroys RAX, RCX, RSI, RDI.
;-----------------------------------------------------------------------
MEMMOVE         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        test    rcx, rcx
        jz      MM_RET
        cmp     rdi, rsi
        jbe     MM_FORWARD
        lea     rax, [rsi+rcx]
        cmp     rdi, rax
        jae     MM_FORWARD
        lea     rsi, [rsi+rcx-1]        ;Overlapping: copy from the top
        lea     rdi, [rdi+rcx-1]
        std
        mov     rax, rcx
        and     rcx, 7
        rep     movsb                   ;Odd bytes at the top
        mov     rcx, rax
        shr     rcx, 3
        jz      MM_DONE
        sub     rsi, 7
        sub     rdi, 7
        rep     movsq                   ;Then whole qwords
MM_DONE:
        cld
MM_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
MM_FORWARD:
        rep     movsb
        lea     rsp, [rbp]
        pop     rbp
        ret
MEMMOVE         ENDP

;=======================================================================
; UTF-8
;=======================================================================

;-----------------------------------------------------------------------
; Decode the character at RSI (which must be before LAST_CHAR).  Returns
; EAX = code point and RSI = the next character.  A byte that doesn't
; start a valid sequence counts as one Latin-1 character.
; Destroys R9-R11.
;-----------------------------------------------------------------------
DECODE          PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        movzx   eax, byte ptr [rsi]
        NEXTP   rsi
        cmp     eax, 0C2H               ;ASCII, or not a lead byte
        jb      DC_RET
        cmp     eax, 0F4H
        ja      DC_RET
        push    rsi                     ;In case the sequence is bad
        mov     r10d, eax               ;R10 = lead byte
        cmp     eax, 0E0H
        jae     DC_3
        and     eax, 1FH
        mov     r9d, 1
        jmp     DC_LOOP
DC_3:   cmp     eax, 0F0H
        jae     DC_4
        and     eax, 0FH
        mov     r9d, 2
        jmp     DC_LOOP
DC_4:   and     eax, 07H
        mov     r9d, 3
DC_LOOP:
        cmp     rsi, LAST_CHAR
        jae     DC_BAD
        movzx   r11d, byte ptr [rsi]
        test    r11b, 80H               ;Must be 10xxxxxx
        jz      DC_BAD
        test    r11b, 40H
        jnz     DC_BAD
        shl     eax, 6
        and     r11d, 3FH
        or      eax, r11d
        NEXTP   rsi
        dec     r9d
        jnz     DC_LOOP
        cmp     r10d, 0E0H              ;Two bytes: always OK
        jb      DC_OK
        cmp     r10d, 0F0H
        jae     DC_CHK4
        cmp     eax, 800H               ;Three bytes: no overlong
        jb      DC_BAD                  ; forms or surrogates
        mov     r11d, eax
        and     r11d, 0FFFFF800H
        cmp     r11d, 0D800H
        je      DC_BAD
        jmp     DC_OK
DC_CHK4:
        cmp     eax, 10000H             ;Four bytes: 10000h-10FFFFh
        jb      DC_BAD
        cmp     eax, 10FFFFH
        ja      DC_BAD
DC_OK:
        add     rsp, 8
DC_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
DC_BAD:
        pop     rsi                     ;Just the lead byte, as Latin-1
        mov     eax, r10d
        lea     rsp, [rbp]
        pop     rbp
        ret
DECODE          ENDP

;-----------------------------------------------------------------------
; Same as DECODE, for a zero-terminated string in ordinary memory.
;-----------------------------------------------------------------------
DECODE_STR      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        movzx   eax, byte ptr [rsi]
        inc     rsi
        cmp     eax, 0C2H
        jb      DS_RET
        cmp     eax, 0F4H
        ja      DS_RET
        push    rsi
        mov     r10d, eax
        cmp     eax, 0E0H
        jae     DS_3
        and     eax, 1FH
        mov     r9d, 1
        jmp     DS_LOOP
DS_3:   cmp     eax, 0F0H
        jae     DS_4
        and     eax, 0FH
        mov     r9d, 2
        jmp     DS_LOOP
DS_4:   and     eax, 07H
        mov     r9d, 3
DS_LOOP:
        movzx   r11d, byte ptr [rsi]
        test    r11b, 80H
        jz      DS_BAD
        test    r11b, 40H
        jnz     DS_BAD
        shl     eax, 6
        and     r11d, 3FH
        or      eax, r11d
        inc     rsi
        dec     r9d
        jnz     DS_LOOP
        cmp     r10d, 0E0H
        jb      DS_OK
        cmp     r10d, 0F0H
        jae     DS_CHK4
        cmp     eax, 800H
        jb      DS_BAD
        mov     r11d, eax
        and     r11d, 0FFFFF800H
        cmp     r11d, 0D800H
        je      DS_BAD
        jmp     DS_OK
DS_CHK4:
        cmp     eax, 10000H
        jb      DS_BAD
        cmp     eax, 10FFFFH
        ja      DS_BAD
DS_OK:
        add     rsp, 8
DS_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
DS_BAD:
        pop     rsi
        mov     eax, r10d
        lea     rsp, [rbp]
        pop     rbp
        ret
DECODE_STR      ENDP

;-----------------------------------------------------------------------
; Move RSI back to the start of the previous character.  RSI must be
; after TEXT_BEG.  Destroys RAX, R8-R11.
;-----------------------------------------------------------------------
PREV_CHAR       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     r8, rsi                 ;Where we started
        PREVP   rsi
        movzx   eax, byte ptr [rsi]
        and     al, 0C0H
        cmp     al, 80H                 ;Continuation byte?
        jne     PC_RET                  ;If not, that's the character
        push    rsi                     ;Fallback: just one byte back
        push    rcx
        mov     ecx, 3
PC_BACK:
        cmp     rsi, TEXT_BEG           ;Look back for the lead byte
        je      PC_FAIL
        PREVP   rsi
        movzx   eax, byte ptr [rsi]
        and     al, 0C0H
        cmp     al, 80H
        jne     PC_LEAD
        dec     ecx
        jnz     PC_BACK
        jmp     PC_FAIL
PC_LEAD:
        push    rsi
        call    DECODE                  ;Does that character end
        cmp     rsi, r8                 ; where we started?
        pop     rsi
        jne     PC_FAIL
        pop     rcx
        add     rsp, 8
        lea     rsp, [rbp]
        pop     rbp
        ret
PC_FAIL:
        pop     rcx
        pop     rsi
PC_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
PREV_CHAR       ENDP

;-----------------------------------------------------------------------
; Write code point EAX as UTF-8 at RDI.  Returns RCX = byte count.
;-----------------------------------------------------------------------
ENCODE_UTF8     PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rdx
        cmp     eax, 80H
        jae     EN_2
        mov     [rdi], al
        mov     ecx, 1
        jmp     EN_RET
EN_2:   cmp     eax, 800H
        jae     EN_3
        mov     edx, eax
        shr     edx, 6
        or      dl, 0C0H
        mov     [rdi], dl
        and     al, 3FH
        or      al, 80H
        mov     [rdi+1], al
        mov     ecx, 2
        jmp     EN_RET
EN_3:   cmp     eax, 10000H
        jae     EN_4
        mov     edx, eax
        shr     edx, 12
        or      dl, 0E0H
        mov     [rdi], dl
        mov     edx, eax
        shr     edx, 6
        and     dl, 3FH
        or      dl, 80H
        mov     [rdi+1], dl
        and     al, 3FH
        or      al, 80H
        mov     [rdi+2], al
        mov     ecx, 3
        jmp     EN_RET
EN_4:   mov     edx, eax
        shr     edx, 18
        or      dl, 0F0H
        mov     [rdi], dl
        mov     edx, eax
        shr     edx, 12
        and     dl, 3FH
        or      dl, 80H
        mov     [rdi+1], dl
        mov     edx, eax
        shr     edx, 6
        and     dl, 3FH
        or      dl, 80H
        mov     [rdi+2], dl
        and     al, 3FH
        or      al, 80H
        mov     [rdi+3], al
        mov     ecx, 4
EN_RET:
        pop     rdx
        lea     rsp, [rbp]
        pop     rbp
        ret
ENCODE_UTF8     ENDP

;-----------------------------------------------------------------------
; Turn code point EAX into the character to show for it (control
; characters become pictures).  Destroys R11.
;-----------------------------------------------------------------------
GLYPH           PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     eax, 20H                ;Control character?
        jae     GL_1
        lea     r11, GLYPH_TABLE
        movzx   eax, word ptr [r11+rax*2]
        lea     rsp, [rbp]
        pop     rbp
        ret
GL_1:   cmp     eax, 7FH
        jne     GL_2
        mov     eax, 2302H              ;House, as in code page 437
        lea     rsp, [rbp]
        pop     rbp
        ret
GL_2:   cmp     eax, 80H
        jb      GL_RET
        cmp     eax, 0A0H               ;C1 controls
        jae     GL_RET
        mov     eax, 0FFFDH             ;Replacement character
GL_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
GLYPH           ENDP

;=======================================================================
; Console input and output
;=======================================================================

;-----------------------------------------------------------------------
; Write the character in AX to the screen buffer at RDI and advance RDI
; one cell.  Destroys R11.
;-----------------------------------------------------------------------
WRITE_INVERSE   PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     word ptr [rdi], ax      ;CHAR_INFO.Char
        movzx   r11d, INVERSE
        mov     word ptr [rdi+2], r11w  ;CHAR_INFO.Attributes
        add     rdi, 4
        lea     rsp, [rbp]
        pop     rbp
        ret
WRITE_INVERSE   ENDP

WRITE_NORMAL    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     word ptr [rdi], ax
        movzx   r11d, NORMAL
        mov     word ptr [rdi+2], r11w
        add     rdi, 4
        lea     rsp, [rbp]
        pop     rbp
        ret
WRITE_NORMAL    ENDP

;-----------------------------------------------------------------------
; This moves the cursor to the row/column in EDX.  All registers saved.
;-----------------------------------------------------------------------
SET_CURSOR      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        PUSH_VOLATILE
        movzx   eax, dx                 ;Column
        movzx   ecx, COLUMNS
        cmp     eax, ecx                ;Keep cursor inside the window
        jb      SC_COL_OK
        lea     eax, [rcx-1]
SC_COL_OK:
        shr     edx, 16                 ;Row
        add     ax, WIN_LEFT
        add     dx, WIN_TOP
        shl     edx, 16
        movzx   eax, ax
        or      edx, eax                ;COORD {X, Y}
        mov     rcx, H_OUT
        CALLWIN SetConsoleCursorPosition
        POP_VOLATILE
        lea     rsp, [rbp]
        pop     rbp
        ret
SET_CURSOR      ENDP

;-----------------------------------------------------------------------
; Copy the screen buffer to the console window, a band of rows at a
; time so that no single call is very large.  All registers saved.
;-----------------------------------------------------------------------
FLUSH_SCREEN    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        PUSH_VOLATILE
        push    rbx
        push    rsi
        movzx   ebx, COLUMNS
        mov     eax, FLUSH_CELLS
        xor     edx, edx
        div     ebx
        mov     BAND_ROWS, eax          ;Rows per call
        xor     esi, esi                ;First row of this band
FS_BAND:
        movzx   eax, SCREEN_ROWS
        cmp     esi, eax
        jae     FS_DONE
        sub     eax, esi
        cmp     eax, BAND_ROWS
        jbe     FS_COUNT
        mov     eax, BAND_ROWS
FS_COUNT:
        mov     BAND_N, eax
        movzx   ecx, WIN_LEFT
        mov     WRITE_RECT, cx          ;Left
        lea     ecx, [rcx+rbx-1]
        mov     WRITE_RECT+4, cx        ;Right
        movzx   ecx, WIN_TOP
        add     ecx, esi
        mov     WRITE_RECT+2, cx        ;Top
        lea     ecx, [rcx+rax-1]
        mov     WRITE_RECT+6, cx        ;Bottom
        mov     r8d, eax
        shl     r8d, 16
        or      r8d, ebx                ;Buffer size COORD
        xor     r9d, r9d                ;Buffer origin COORD {0, 0}
        mov     eax, esi
        imul    eax, ebx
        shl     rax, 2
        mov     rdx, SCREEN_BUF
        add     rdx, rax
        mov     rcx, H_OUT
        lea     rax, WRITE_RECT
        CALLWIN WriteConsoleOutputW, rax
        add     esi, BAND_N
        jmp     FS_BAND
FS_DONE:
        pop     rsi
        pop     rbx
        POP_VOLATILE
        lea     rsp, [rbp]
        pop     rbp
        ret
FLUSH_SCREEN    ENDP

;-----------------------------------------------------------------------
; Returns EAX=1 if INPUT_REC holds a key-down for a real key (not just
; Shift, Ctrl, Alt, CapsLock, NumLock, ScrollLock or Windows keys).
; ECX destroyed.
;-----------------------------------------------------------------------
CHECK_KEY_EVENT PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        xor     eax, eax
        cmp     word ptr INPUT_REC+IR_EVENTTYPE, KEY_EVENT
        jne     CK_RET
        cmp     dword ptr INPUT_REC+IR_KEYDOWN, 0
        je      CK_RET
        movzx   ecx, word ptr INPUT_REC+IR_VKEY
        cmp     ecx, 10H                ;VK_SHIFT
        je      CK_RET
        cmp     ecx, 11H                ;VK_CONTROL
        je      CK_RET
        cmp     ecx, 12H                ;VK_MENU (Alt)
        je      CK_RET
        cmp     ecx, 14H                ;VK_CAPITAL
        je      CK_RET
        cmp     ecx, 5BH                ;VK_LWIN, VK_RWIN, VK_APPS
        jb      CK_NOT_WIN
        cmp     ecx, 5DH
        jbe     CK_RET
CK_NOT_WIN:
        cmp     ecx, 90H                ;VK_NUMLOCK
        je      CK_RET
        cmp     ecx, 91H                ;VK_SCROLL
        je      CK_RET
        inc     eax
CK_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
CHECK_KEY_EVENT ENDP

;-----------------------------------------------------------------------
; Replacement for INT 16h function 0.  Flushes the screen, waits for a
; key and returns AL = ASCII code (0 for extended keys, 0FFh for a
; non-ASCII character), AH = scan code, and KEY_CHAR = the Unicode
; character (0 if none).  Ctrl+arrows, Ctrl+PgUp/PgDn and Alt+keys
; return BIOS-style extended codes.  If the window changes size, the
; screen is redrawn and RESIZE_KEY is returned.  All registers except
; RAX are saved.
;-----------------------------------------------------------------------
GET_KEY         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rcx
        push    rdx
        push    r8
        push    r9
        push    r10
        push    r11
        mov     BUSY, 0                 ;The text is safe to save now
GK_TOP:
        call    CHECK_RESIZE            ;Window changed size?
        jc      GK_RESIZED
        call    FLUSH_SCREEN
GK_WAIT:
        mov     rcx, H_IN
        mov     edx, 250                ;Look at the window size again
        CALLWIN WaitForSingleObject     ; every 1/4 second
        cmp     eax, WAIT_TIMEOUT
        je      GK_TOP
        mov     rcx, H_IN
        lea     rdx, INPUT_REC
        mov     r8d, 1
        lea     r9, NUM_EVENTS
        CALLWIN ReadConsoleInputW
        test    eax, eax
        jz      INPUT_LOST              ;Console input lost - quit
        cmp     word ptr INPUT_REC+IR_EVENTTYPE, WINDOW_BUFFER_SIZE_EVENT
        je      GK_TOP
        call    CHECK_KEY_EVENT
        test    eax, eax
        jz      GK_WAIT
        movzx   ecx, word ptr INPUT_REC+IR_CHAR    ;UTF-16 unit
        mov     eax, ecx
        and     eax, 0FC00H
        cmp     eax, 0D800H             ;High surrogate: wait for the
        jne     GK_NOT_HIGH             ; low half
        mov     HIGH_SURR, ecx
        jmp     GK_WAIT
GK_NOT_HIGH:
        cmp     eax, 0DC00H             ;Low surrogate: combine them
        jne     GK_NOT_LOW
        mov     eax, HIGH_SURR
        test    eax, eax
        jz      GK_WAIT
        sub     eax, 0D800H
        shl     eax, 10
        sub     ecx, 0DC00H
        add     ecx, eax
        add     ecx, 10000H
        mov     HIGH_SURR, 0
GK_NOT_LOW:
        mov     KEY_CHAR, ecx
        xor     eax, eax
        mov     ah, byte ptr INPUT_REC+IR_SCAN
        mov     al, cl
        cmp     ecx, 80H
        jb      GK_ASCII
        mov     al, 0FFH                ;Non-ASCII (see KEY_CHAR)
GK_ASCII:
        mov     edx, dword ptr INPUT_REC+IR_CTRLSTATE
        test    edx, ALT_PRESSED        ;Alt (but not AltGr = Ctrl+Alt)
        jz      GK_NO_ALT               ; gives a BIOS-style Alt code
        test    edx, CTRL_PRESSED
        jnz     GK_NO_ALT
        xor     al, al
        mov     KEY_CHAR, 0
GK_NO_ALT:
        test    edx, CTRL_PRESSED
        jz      GK_DONE
        movzx   ecx, word ptr INPUT_REC+IR_VKEY
        cmp     ecx, 25H                ;VK_LEFT
        jne     GK_1
        mov     ax, 7300H
        jmp     GK_CTRL
GK_1:   cmp     ecx, 27H                ;VK_RIGHT
        jne     GK_2
        mov     ax, 7400H
        jmp     GK_CTRL
GK_2:   cmp     ecx, 21H                ;VK_PRIOR (PgUp)
        jne     GK_3
        mov     ax, 8400H
        jmp     GK_CTRL
GK_3:   cmp     ecx, 22H                ;VK_NEXT (PgDn)
        jne     GK_DONE
        mov     ax, 7600H
GK_CTRL:
        mov     KEY_CHAR, 0
GK_DONE:
        push    rax
        mov     eax, 1
        xchg    BUSY, eax               ;Back to work, unless the
        cmp     HANDLER_ACTIVE, 0       ; window is being closed
        pop     rax
        jne     GK_PARK
        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     rdx
        pop     rcx
        lea     rsp, [rbp]
        pop     rbp
        ret
GK_RESIZED:
        mov     KEY_CHAR, 0
        mov     eax, RESIZE_KEY
        jmp     GK_DONE
INPUT_LOST:
        mov     EXIT_CODE, 1            ;Ended abnormally
        lea     rsp, [rbp]
        pop     rbp
        jmp     QUIT
GK_PARK:
        mov     BUSY, 0                 ;Wait here while the control
GK_SLEEP:                               ; handler saves the text and
        mov     ecx, INFINITE           ; Windows ends the program
        CALLWIN Sleep
        jmp     GK_SLEEP
GET_KEY         ENDP

;-----------------------------------------------------------------------
; Check whether the console window has changed size or position.  If
; it has, update the geometry, redraw the text and prompt, set RESIZED
; and return CF=1.  All registers saved.
;-----------------------------------------------------------------------
CHECK_RESIZE    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        PUSH_VOLATILE
        push    rbx
        push    rsi
        push    rdi
        mov     rcx, H_OUT
        lea     rdx, CSBI
        CALLWIN GetConsoleScreenBufferInfo
        test    eax, eax
        jz      CR_SAME
        call    SET_GEOMETRY
        jnc     CR_SAME
        mov     RESIZED, 1
        call    DISPLAY_SCREEN          ;Redraw for the new size
        call    REDO_PROMPT
        pop     rdi
        pop     rsi
        pop     rbx
        POP_VOLATILE
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
CR_SAME:
        pop     rdi
        pop     rsi
        pop     rbx
        POP_VOLATILE
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
CHECK_RESIZE    ENDP

;-----------------------------------------------------------------------
; Replacement for INT 16h function 1.  Returns EAX != 0 if a key is
; waiting.  Non-key events in front of it are discarded.
;-----------------------------------------------------------------------
KEY_READY       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rcx
        push    rdx
        push    r8
        push    r9
        push    r10
        push    r11
KR_LOOP:
        mov     rcx, H_IN
        lea     rdx, NUM_EVENTS
        CALLWIN GetNumberOfConsoleInputEvents
        test    eax, eax
        jz      KR_NONE
        cmp     NUM_EVENTS, 0
        je      KR_NONE
        mov     rcx, H_IN
        lea     rdx, INPUT_REC
        mov     r8d, 1
        lea     r9, NUM_EVENTS2
        CALLWIN PeekConsoleInputW
        test    eax, eax
        jz      KR_NONE
        cmp     NUM_EVENTS2, 0
        je      KR_NONE
        call    CHECK_KEY_EVENT
        test    eax, eax
        jnz     KR_DONE
        mov     rcx, H_IN               ;Not a key - throw it away
        lea     rdx, INPUT_REC
        mov     r8d, 1
        lea     r9, NUM_EVENTS2
        CALLWIN ReadConsoleInputW
        jmp     KR_LOOP
KR_NONE:
        xor     eax, eax
KR_DONE:
        pop     r11
        pop     r10
        pop     r9
        pop     r8
        pop     rdx
        pop     rcx
        lea     rsp, [rbp]
        pop     rbp
        ret
KEY_READY       ENDP

;-----------------------------------------------------------------------
; Sound the bell.  All registers saved.
;-----------------------------------------------------------------------
BEEP            PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        PUSH_VOLATILE
        mov     ecx, 750
        mov     edx, 100
        CALLWIN Beep
        POP_VOLATILE
        lea     rsp, [rbp]
        pop     rbp
        ret
BEEP            ENDP

;-----------------------------------------------------------------------
; Write the zero-terminated UTF-8 string at RSI to standard output.
;-----------------------------------------------------------------------
PRINT_STR       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        PUSH_VOLATILE
        push    rsi
        push    rdi
        lea     rdi, WIDE_BUF           ;Convert to UTF-16
        xor     r8d, r8d
PS_LOOP:
        cmp     byte ptr [rsi], 0
        je      PS_CONSOLE
        cmp     r8d, 510
        jae     PS_CONSOLE
        call    DECODE_STR
        cmp     eax, 0FFFFH
        jbe     PS_STORE
        sub     eax, 10000H             ;Above U+FFFF: a surrogate pair
        mov     ecx, eax
        shr     eax, 10
        add     eax, 0D800H
        mov     word ptr [rdi+r8*2], ax
        inc     r8d
        mov     eax, ecx
        and     eax, 3FFH
        add     eax, 0DC00H
PS_STORE:
        mov     word ptr [rdi+r8*2], ax
        inc     r8d
        jmp     PS_LOOP
PS_CONSOLE:
        mov     rcx, H_OUT
        lea     rdx, WIDE_BUF
        lea     r9, IO_COUNT
        CALLWIN WriteConsoleW, 0
        test    eax, eax
        jnz     PS_DONE
        mov     rdx, [rsp+8]            ;Not a console (redirected):
        mov     rdi, rdx                ; write the bytes as they are
        call    STRLEN
        mov     r8, rax
        mov     rcx, H_OUT
        lea     r9, IO_COUNT
        CALLWIN WriteFile, 0
PS_DONE:
        pop     rdi
        pop     rsi
        POP_VOLATILE
        lea     rsp, [rbp]
        pop     rbp
        ret
PRINT_STR       ENDP

;-----------------------------------------------------------------------
; This computes the screen buffer address for the row/column in EDX.
; Returns the result in RDI.  Destroys RAX and R11.
;-----------------------------------------------------------------------
POSITION        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     eax, edx
        shr     eax, 16                 ;Row
        movzx   r11d, COLUMNS
        imul    eax, r11d               ;Times columns per row
        movzx   r11d, dx
        add     eax, r11d               ;Add in the column number
        mov     rdi, SCREEN_BUF
        lea     rdi, [rdi+rax*4]        ;4 bytes per CHAR_INFO
        lea     rsp, [rbp]
        pop     rbp
        ret
POSITION        ENDP

;-----------------------------------------------------------------------
; This erases from the location in EDX to the right edge of the screen
;-----------------------------------------------------------------------
ERASE_EOL       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        call    POSITION                ;Find screen offset
        movzx   ecx, COLUMNS            ;Get screen size
        movzx   eax, dx
        sub     ecx, eax                ;Subtract current position
        jbe     NO_CLEAR
ERASE_LOOP:
        mov     ax, " "                 ;Write blanks to erase
        call    WRITE_NORMAL            ;Display it
        loop    ERASE_LOOP
NO_CLEAR:
        lea     rsp, [rbp]
        pop     rbp
        ret
ERASE_EOL       ENDP

;-----------------------------------------------------------------------
; This displays the function key prompt, the modified flag ("*") and
; the insert mode state (I or O).  The keys are separated by spaces
; when the window is wide enough; otherwise the spaces are left out
; (the labels are in inverse video, so they still stand apart).
; The prompt is clipped so the last two columns are always free.
;-----------------------------------------------------------------------
PROMPT_PUT      MACRO   writer
        LOCAL   pp_skip
        cmp     r9d, r10d               ;Room left before the last two
        jae     pp_skip                 ; columns?
        call    writer
        inc     r9d
pp_skip:
ENDM

REDO_PROMPT     PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     PROMPT_DIRTY, 0
        movzx   edx, ROWS               ;Put prompt at last row
        inc     edx
        shl     edx, 16                 ;And column 0
        call    POSITION                ;Convert to screen offset
        movzx   r10d, COLUMNS
        sub     r10d, 2                 ;R10 = columns available
        xor     r8d, r8d                ;R8 = 1 to leave out the spaces
        cmp     r10d, PROMPT_WIDE
        jae     PR_SPACED
        inc     r8d
PR_SPACED:
        xor     r9d, r9d                ;R9 = column
        lea     rsi, PROMPT_STRING
KEY_LOOP:
        cmp     byte ptr [rsi], 0       ;Last key in prompt?
        je      PROMPT_DONE
        mov     ax, "F"                 ;Display an "F"
        PROMPT_PUT WRITE_NORMAL
KEY_DIGITS:
        lodsb                           ;Then the key number
        cmp     al, "0"
        jb      TEXT_LOOP
        cmp     al, "9"
        ja      TEXT_LOOP
        movzx   eax, al
        PROMPT_PUT WRITE_NORMAL
        jmp     KEY_DIGITS
TEXT_LOOP:
        or      al, al                  ;Last letter in word?
        jz      WORD_DONE
        movzx   eax, al
        PROMPT_PUT WRITE_INVERSE        ;Display the letter
        lodsb
        jmp     TEXT_LOOP               ;Do the next letter
WORD_DONE:
        test    r8d, r8d                ;Room for spaces?
        jnz     KEY_LOOP
        mov     ax, " "                 ;Display a space
        PROMPT_PUT WRITE_NORMAL
        jmp     KEY_LOOP
PROMPT_DONE:
        mov     eax, r9d                ;Erase the rest of the row
PD_COL:
        movzx   edx, ROWS
        inc     edx
        shl     edx, 16
        mov     dx, ax
        call    ERASE_EOL
        movzx   edx, ROWS               ;Last two columns: "*" and I/O
        inc     edx
        shl     edx, 16
        movzx   eax, COLUMNS
        sub     eax, 2
        mov     dx, ax
        call    POSITION
        mov     ax, " "
        cmp     MODIFIED, 0             ;Unsaved changes?
        je      PD_MOD
        mov     ax, "*"
PD_MOD:
        call    WRITE_NORMAL
        mov     ax, "O"                 ;Write an "O"
        cmp     INSERT_MODE, 0          ;In insert mode?
        je      OVERSTRIKE
        mov     ax, "I"                 ;Write an "I"
OVERSTRIKE:
        call    WRITE_NORMAL
        lea     rsp, [rbp]
        pop     rbp
        ret
REDO_PROMPT     ENDP

;-----------------------------------------------------------------------
; This displays the file buffer on the screen.
;-----------------------------------------------------------------------
DISPLAY_SCREEN  PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rsi, TOP_OF_SCREEN      ;Point to first char on screen
        xor     edx, edx                ;Start at first row
        lea     rsp, [rbp]
        pop     rbp
        jmp     DISPLAY_ROWS
DISPLAY_SCREEN  ENDP

; This redraws from the cursor's row to the bottom only
DISPLAY_BOTTOM  PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        call    FIND_START              ;Find first character on this row
        mov     edx, CUR_POSN           ;Get current cursor row
        lea     rsp, [rbp]
        pop     rbp
        jmp     DISPLAY_ROWS
DISPLAY_BOTTOM  ENDP

; Display lines from RSI, starting at the row in EDX, to the last row
DISPLAY_ROWS    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
NEXT_ROW:
        push    rdx
        call    DISPLAY_LINE            ;Display a line
        pop     rdx
        add     edx, 10000H             ;Move to the next row
        mov     eax, edx
        shr     eax, 16
        cmp     ax, ROWS                ;At end of screen yet?
        jbe     NEXT_ROW                ;Do all the rows
        lea     rsp, [rbp]
        pop     rbp
        ret
DISPLAY_ROWS    ENDP

;-----------------------------------------------------------------------
; This subroutine displays a single line to the screen.  EDX holds the
; row number (high word), RSI has the position in the file buffer.
; Tabs are expanded.  Adjustment is made for side shift.  Returns RSI
; at the start of the next line.
;-----------------------------------------------------------------------
DISPLAY_CURRENT PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        call    FIND_START
        mov     edx, CUR_POSN
        lea     rsp, [rbp]
        pop     rbp
        jmp     DISPLAY_LINE
DISPLAY_CURRENT ENDP

DISPLAY_LINE    PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        and     edx, 0FFFF0000H         ;Start at column zero
        mov     MARGIN_COUNT, 0
        mov     ecx, edx                ;Use CX to count the columns
        call    POSITION                ;Compute offset into video
NEXT_CHAR:
        cmp     rsi, LAST_CHAR          ;At end of file?
        jae     LINE_DONE
        cmp     byte ptr [rsi], CR      ;Is it a carriage return?
        je      FOUND_CR                ;Quit when a CR is found
        cmp     byte ptr [rsi], TAB     ;Is this a Tab character
        je      EXPAND_TAB              ;If yes, expand to spaces
        mov     rbx, rsi                ;Remember where it started
        call    DECODE                  ;Get next character
        call    PUT_CHAR                ;Put character onto screen
TAB_DONE:
        cmp     cx, COLUMNS             ;At right edge of screen?
        jb      NEXT_CHAR
        cmp     rsi, LAST_CHAR
        jae     NOT_BEYOND
        cmp     byte ptr [rsi], CR      ;Is this the end of the line?
        je      NOT_BEYOND
        sub     rdi, 4                  ;Backup one character
        mov     ax, 2666H               ;Show a diamond
        call    WRITE_INVERSE           ;In inverse video
        cmp     cx, 2
        jb      NOT_BEYOND
        test    word ptr [rdi-6], COMMON_LVB_LEADING_BYTE
        jz      NOT_BEYOND              ;Half a wide character left?
        and     word ptr [rdi-6], 0FFH  ;Make it a plain space
        mov     word ptr [rdi-8], " "
NOT_BEYOND:
        lea     rsp, [rbp]
        pop     rbp
        jmp     FIND_NEXT               ;Find start of next line
FOUND_CR:
        NEXTP   rsi                     ;Skip the CR
        cmp     rsi, LAST_CHAR
        jae     LINE_DONE
        cmp     byte ptr [rsi], LF      ;And a line feed after it
        jne     LINE_DONE
        NEXTP   rsi
LINE_DONE:
        mov     edx, ecx
        lea     rsp, [rbp]
        pop     rbp
        jmp     ERASE_EOL               ;Erase the rest of the line
EXPAND_TAB:
        mov     rbx, rsi
        NEXTP   rsi
        movzx   eax, cx                 ;Column within the line
        add     eax, MARGIN_COUNT
        call    NEXT_TAB
        mov     edx, eax                ;EDX = the next tab stop
TAB_LOOP:
        mov     eax, " "                ;Convert Tabs to spaces
        call    PUT_CHAR
        cmp     cx, COLUMNS             ;Stop at the right edge
        jae     TAB_DONE
        movzx   eax, cx
        add     eax, MARGIN_COUNT
        cmp     eax, edx                ;At the tab stop?
        jb      TAB_LOOP                ;If not keep adding spaces
        jmp     TAB_DONE
DISPLAY_LINE    ENDP

;-----------------------------------------------------------------------
; This displays code point EAX, which started at RBX in the file, at
; column CX of the screen row at RDI.  If it is marked, it is shown in
; inverse video.  Columns left of the margin are skipped (half of a
; wide character there shows as a space).  Destroys RAX, R9-R11.
;-----------------------------------------------------------------------
PUT_CHAR        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        call    CP_WIDTH                ;R11D = 1 or 2 columns
        mov     r10d, MARGIN_COUNT      ;Get distance to left margin
        cmp     r10d, LEFT_MARGIN       ;Are we inside left margin?
        jae     PCH_SHOW                ;If yes, show the character
        add     r10d, r11d
        cmp     r10d, LEFT_MARGIN       ;Does it cross the margin?
        ja      PCH_SPLIT
        mov     MARGIN_COUNT, r10d
        lea     rsp, [rbp]
        pop     rbp
        ret
PCH_SPLIT:
        mov     r10d, LEFT_MARGIN
        mov     MARGIN_COUNT, r10d
        mov     eax, " "                ;Show the visible half as blank
PCH_SHOW:
        movzx   r10d, NORMAL
        cmp     rbx, MARK_START         ;Is this character marked?
        jb      PCH_DRAW
        cmp     rbx, MARK_END
        jae     PCH_DRAW
        movzx   r10d, INVERSE           ;Marked characters shown inverse
PCH_DRAW:
        lea     rsp, [rbp]
        pop     rbp
        jmp     DRAW_GLYPH
PUT_CHAR        ENDP

;-----------------------------------------------------------------------
; Draw code point EAX with attribute R10W at column CX, cell RDI, and
; advance both.  A wide character takes two cells (marked as leading
; and trailing); one above U+FFFF is sent as a surrogate pair across
; them.  A wide character with no room before the right edge is shown
; as a space.  Destroys RAX, R9, R11.
;-----------------------------------------------------------------------
DRAW_GLYPH      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        call    CP_WIDTH
        mov     r9d, r11d               ;R9D = width
        call    GLYPH                   ;Character to show
        cmp     r9d, 2
        je      DG_WIDE
        cmp     eax, 0FFFFH             ;A narrow character above
        jbe     DG_ONE                  ; U+FFFF can't fit in one cell
        mov     eax, 0FFFDH
DG_ONE:
        call    WRITE_CELL
        inc     cx
        lea     rsp, [rbp]
        pop     rbp
        ret
DG_WIDE:
        movzx   r11d, cx
        inc     r11d
        cmp     r11w, COLUMNS           ;Room for both halves?
        jb      DG_ROOM
        mov     eax, " "
        jmp     DG_ONE
DG_ROOM:
        mov     r9d, eax                ;Both cells hold the character,
        cmp     eax, 0FFFFH             ; or the two surrogates
        jbe     DG_TWO
        sub     eax, 10000H
        mov     r9d, eax
        and     r9d, 3FFH
        add     r9d, 0DC00H             ;Low surrogate
        shr     eax, 10
        add     eax, 0D800H             ;High surrogate
DG_TWO:
        push    r10
        or      r10d, COMMON_LVB_LEADING_BYTE
        call    WRITE_CELL
        mov     r10, [rsp]
        or      r10d, COMMON_LVB_TRAILING_BYTE
        mov     eax, r9d
        call    WRITE_CELL
        pop     r10
        add     cx, 2
        lea     rsp, [rbp]
        pop     rbp
        ret
DRAW_GLYPH      ENDP

; Store character AX with attribute R10W at RDI and advance RDI
WRITE_CELL      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     word ptr [rdi], ax
        mov     word ptr [rdi+2], r10w
        add     rdi, 4
        lea     rsp, [rbp]
        pop     rbp
        ret
WRITE_CELL      ENDP

;-----------------------------------------------------------------------
; Return in R11D the number of columns code point EAX takes: 2 for
; East Asian wide and fullwidth characters and emoji, otherwise 1.
; All other registers saved.
;-----------------------------------------------------------------------
CP_WIDTH        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     r11d, 1
        cmp     eax, 1100H              ;Nothing wide below here
        jb      CW_RET
        push    rcx
        push    rdx
        lea     rdx, WIDE_TABLE
        mov     ecx, WIDE_COUNT
CW_LOOP:
        cmp     eax, [rdx]              ;Before this range?
        jb      CW_DONE                 ;Then not in any
        cmp     eax, [rdx+4]
        jbe     CW_WIDE
        add     rdx, 8
        dec     ecx
        jnz     CW_LOOP
        jmp     CW_DONE
CW_WIDE:
        mov     r11d, 2
CW_DONE:
        pop     rdx
        pop     rcx
CW_RET:
        lea     rsp, [rbp]
        pop     rbp
        ret
CP_WIDTH        ENDP

;-----------------------------------------------------------------------
; Return in EAX the tab stop after column EAX.  Destroys R11.
;-----------------------------------------------------------------------
NEXT_TAB        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rdx
        xor     edx, edx
        mov     r11d, TAB_WIDTH
        div     r11d
        inc     eax
        mul     r11d
        pop     rdx
        lea     rsp, [rbp]
        pop     rbp
        ret
NEXT_TAB        ENDP

;-----------------------------------------------------------------------
; This routine adds the ASCII character in AL into the file.  In insert
; mode, remaining characters are pushed forward.  If a CR is inserted, a
; LF is added also.  INSERT_CHAR inserts the Unicode code point in EAX.
;-----------------------------------------------------------------------
INSERT_KEY      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     al, CR                  ;Was this a carriage return
        je      NEW_LINE
        movzx   eax, al
        lea     rsp, [rbp]
        pop     rbp
        jmp     INSERT_CHAR
NEW_LINE:
        mov     word ptr KEY_BYTES, LF*256+CR
        lea     rsi, KEY_BYTES
        mov     ecx, 2
        call    INSERT_BYTES            ;Store the CR and LF
        jc      NL_FULL
        call    DISPLAY_BOTTOM          ;Repaint bottom of the screen
        call    HOME                    ;Cursor to start of line
        lea     rsp, [rbp]
        pop     rbp
        jmp     DOWN                    ;Move down to the new line
NL_FULL:
        lea     rsp, [rbp]
        pop     rbp
        ret
INSERT_KEY      ENDP

INSERT_CHAR     PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        lea     rdi, KEY_BYTES
        call    ENCODE_UTF8             ;RCX = number of bytes
        cmp     INSERT_MODE, 0          ;In insert mode?
        jne     INSERT_IT
        mov     rsi, CURSOR
        cmp     rsi, LAST_CHAR          ;At end of file?
        je      INSERT_IT
        cmp     byte ptr [rsi], CR      ;At end of line?
        je      INSERT_IT
        push    rcx
        call    DEL_ONE_CHAR            ;Overstrike: remove the old one
        pop     rcx
INSERT_IT:
        lea     rsi, KEY_BYTES
        call    INSERT_BYTES            ;Insert character in file
        jc      FILE_FULL
ADVANCE_CUR:
        or      DIRTY_BITS, 4           ;Current line is dirty
        call    RIGHT                   ;Move cursor to next letter
FILE_FULL:
        lea     rsp, [rbp]
        pop     rbp
        ret
INSERT_CHAR     ENDP

;-----------------------------------------------------------------------
; This subroutine moves the cursor along its line to the saved cursor
; column (a column within the line).  On entry RSI is the start of the
; line and EDX has the row.  The cursor lands on the character that
; covers that column, or at the end of a shorter line.
;-----------------------------------------------------------------------
SHIFT_RIGHT     PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        and     edx, 0FFFF0000H
        mov     CUR_POSN, edx           ;Row, column 0
        push    rbx
        mov     ebx, SAVE_COLUMN        ;Column to aim for
        xor     ecx, ecx                ;Column we are at
SR_LOOP:
        cmp     ecx, ebx                ;There yet?
        jae     SR_DONE
        cmp     rsi, LAST_CHAR
        jae     SR_DONE
        cmp     byte ptr [rsi], CR      ;End of the line?
        je      SR_DONE
        mov     rdx, rsi                ;Where this character starts
        cmp     byte ptr [rsi], TAB
        jne     SR_NOT_TAB
        mov     eax, ecx
        call    NEXT_TAB
        NEXTP   rsi
        jmp     SR_CHECK
SR_NOT_TAB:
        call    DECODE                  ;Step over the character
        call    CP_WIDTH
        lea     eax, [rcx+r11]          ;Column after this character
SR_CHECK:
        cmp     eax, ebx                ;Would it go past?
        ja      SR_BACK
        mov     ecx, eax
        jmp     SR_LOOP
SR_BACK:
        mov     rsi, rdx                ;Stay on this character
SR_DONE:
        pop     rbx
        mov     CURSOR, rsi
        lea     rsp, [rbp]
        pop     rbp
        jmp     CURSOR_COL
SHIFT_RIGHT     ENDP

;-----------------------------------------------------------------------
; This subroutine skips past the CR and LF at RSI.  RSI returns new ptr
;-----------------------------------------------------------------------
SKIP_CR_LF      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        cmp     rsi, LAST_CHAR          ;At last char in the file?
        jae     NO_SKIP                 ;If yes, dont skip anything
        cmp     byte ptr [rsi], CR      ;Is first character a CR?
        jne     NO_SKIP
        NEXTP   rsi                     ;Look at next character
        cmp     rsi, LAST_CHAR          ;Is it at the end of file?
        jae     NO_SKIP                 ;If yes, dont skip anymore
        cmp     byte ptr [rsi], LF      ;Is next character a line feed?
        jne     NO_SKIP                 ;Skip any line feeds also
        NEXTP   rsi
NO_SKIP:
        lea     rsp, [rbp]
        pop     rbp
        ret
SKIP_CR_LF      ENDP

;-----------------------------------------------------------------------
; This subroutine finds the beginning of the previous line.
;-----------------------------------------------------------------------
FIND_PREVIOUS   PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    CURSOR                  ;Save the cursor location
        call    FIND_CR                 ;Find start of this line
        mov     CURSOR, rsi             ;Save the new cursor
        call    FIND_START              ;Find the start of this line
        pop     CURSOR                  ;Get back starting cursor
        lea     rsp, [rbp]
        pop     rbp
        ret
FIND_PREVIOUS   ENDP

;-----------------------------------------------------------------------
; This searches backward from RSI for the previous carriage return.
; Returns RSI at the CR, or TEXT_BEG if there is none.  Destroys AL, RDI.
;-----------------------------------------------------------------------
FIND_CR         PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rcx
        mov     al, CR                  ;Look for a carriage return
        cmp     rsi, GAP_END            ;Start after the gap?
        jb      FC_LOW
        mov     rcx, rsi                ;Search back to the gap
        sub     rcx, GAP_END
        jz      FC_TO_LOW
        lea     rdi, [rsi-1]
        std                             ;Search backwards
        repne   scasb                   ;Scan for the character
        cld                             ;Restore direction flag
        je      FC_FOUND
FC_TO_LOW:
        mov     rsi, GAP_START          ;Then before the gap
FC_LOW:
        mov     rcx, rsi
        sub     rcx, r15
        jz      FC_NONE
        lea     rdi, [rsi-1]
        std
        repne   scasb
        cld
        je      FC_FOUND
FC_NONE:
        mov     rsi, TEXT_BEG
        pop     rcx
        lea     rsp, [rbp]
        pop     rbp
        ret
FC_FOUND:
        lea     rsi, [rdi+1]
        pop     rcx
        lea     rsp, [rbp]
        pop     rbp
        ret
FIND_CR         ENDP

;-----------------------------------------------------------------------
; This subroutine computes the location of the start of current line.
; Returns RSI pointing to the first character of the current line.
;-----------------------------------------------------------------------
FIND_START      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     rsi, CURSOR             ;Get the current cursor
        cmp     rsi, TEXT_BEG           ;At start of the file?
        je      AT_START                ;If yes, were done
        call    FIND_CR                 ;Find the previous CR
        call    SKIP_CR_LF
AT_START:
        lea     rsp, [rbp]
        pop     rbp
        ret
FIND_START      ENDP

;-----------------------------------------------------------------------
; This finds the start of the next line.  The search is started at RSI.
; On return CF=1 if no CR was found.
;-----------------------------------------------------------------------
FIND_NEXT       PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rcx
        call    FIND_EOL                ;Find the end of this line
        jc      AT_NEXT                 ;If at end of file, return
        call    SKIP_CR_LF              ;Skip past CR and LF
        clc                             ;Indicate end of line found
AT_NEXT:
        pop     rcx
        lea     rsp, [rbp]
        pop     rbp
        ret
FIND_NEXT       ENDP

;-----------------------------------------------------------------------
; This searches for the next carriage return in the file.  The search
; starts at RSI.  Returns RSI at the CR (CF=0) or at end of file (CF=1).
; Destroys AL, RCX, RDI.
;-----------------------------------------------------------------------
FIND_EOL        PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     al, CR                  ;Look for a carriage return
        cmp     rsi, GAP_START          ;Start before the gap?
        jae     FE_HIGH
        mov     rcx, GAP_START          ;Search up to the gap
        sub     rcx, rsi
        mov     rdi, rsi
        repne   scasb                   ;Scan for the character
        je      FE_FOUND
        mov     rsi, GAP_END            ;Then after it
FE_HIGH:
        mov     rcx, LAST_CHAR
        sub     rcx, rsi                ;Count for the search
        jbe     FE_END                  ;If nothing to search, return
        mov     rdi, rsi
        repne   scasb
        je      FE_FOUND
FE_END:
        mov     rsi, LAST_CHAR
        stc                             ;Indicate CR was not found
        lea     rsp, [rbp]
        pop     rbp
        ret
FE_FOUND:
        lea     rsi, [rdi-1]            ;Return the location of the CR
        clc                             ;Indicate the CR was found
        lea     rsp, [rbp]
        pop     rbp
        ret
FIND_EOL        ENDP

;-----------------------------------------------------------------------
; This subroutine positions the screen with the cursor at the row
; selected in EDX (high word).  On entry, RSI holds the cursor position.
;-----------------------------------------------------------------------
LOCATE          PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        mov     ecx, edx
        shr     ecx, 16                 ;Rows to go up
        mov     CURSOR, rsi
        xor     edx, edx                ;Start at top of the screen
        cmp     rsi, TEXT_BEG           ;At start of buffer?
        je      LOCATE_FIRST

        call    FIND_START              ;Get start of this row
        xor     edx, edx                ;Start at top of the screen
        cmp     rsi, TEXT_BEG           ;Is cursor at start of file?
        je      LOCATE_FIRST
        test    ecx, ecx                ;If locating to top row were done
        jz      LOCATE_FIRST
FIND_TOP:
        push    rsi
        push    rcx
        call    FIND_CR                 ;Find previous row
        pop     rcx
        pop     rax
        cmp     byte ptr [rsi], CR
        jne     LOCATE_FIRST
        cmp     rsi, rax                ;Did it change?
        je      LOCATE_DONE             ;If not, quit moving
        add     edx, 10000H             ;Cursor moves to next row
        loop    FIND_TOP

LOCATE_DONE:
        push    CURSOR
        mov     CURSOR, rsi
        call    FIND_START              ;Find start of top of screen
        pop     CURSOR
LOCATE_FIRST:
        mov     TOP_OF_SCREEN, rsi
        mov     CUR_POSN, edx
        call    CURSOR_COL
        mov     eax, LINE_COL
        mov     SAVE_COLUMN, eax
        lea     rsp, [rbp]
        pop     rbp
        ret
LOCATE          ENDP

;-----------------------------------------------------------------------
; This subroutine computes the correct column for the cursor.  No
; inputs.  Unless HSCROLL_OFF is set, the view is scrolled sideways (in
; steps of eight) so the cursor is on screen.  On exit, CUR_POSN is
; set, EDX has the row/column, LINE_COL has the column within the line
; and CUR_WIDTH the width of the character at the cursor.
; Destroys RSI, RAX, R8-R11.
;-----------------------------------------------------------------------
CURSOR_COL      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rbx
        call    FIND_START              ;Find start of this line
        xor     r8d, r8d                ;Column within the line
CC_LOOP:
        cmp     rsi, CURSOR             ;Reached the cursor?
        jae     CC_DONE
        cmp     byte ptr [rsi], TAB     ;Is it a tab?
        jne     CC_CHAR
        mov     eax, r8d                ;Move to the next tab stop
        call    NEXT_TAB
        mov     r8d, eax
        NEXTP   rsi
        jmp     CC_LOOP
CC_CHAR:
        call    DECODE                  ;Step over one character
        call    CP_WIDTH
        add     r8d, r11d
        jmp     CC_LOOP
CC_DONE:
        mov     LINE_COL, r8d
        mov     ebx, 1                  ;Width of the character here
        cmp     rsi, LAST_CHAR
        jae     CC_W
        cmp     byte ptr [rsi], CR
        je      CC_W
        cmp     byte ptr [rsi], TAB
        je      CC_W
        call    DECODE
        call    CP_WIDTH
        mov     ebx, r11d
CC_W:
        mov     CUR_WIDTH, ebx
        cmp     HSCROLL_OFF, 0          ;Keep the view where it is?
        jne     CC_VIEW
        mov     eax, LEFT_MARGIN
        cmp     r8d, eax                ;Left of the window?
        jb      CC_GO_LEFT
        lea     r9d, [r8+rbx]           ;Column after the character
        movzx   r10d, COLUMNS
        add     r10d, eax               ;Column after the window
        cmp     r9d, r10d
        jbe     CC_VIEW                 ;It fits
        movzx   r10d, COLUMNS
        sub     r9d, r10d               ;Smallest margin that fits it,
        add     r9d, 7                  ; rounded up to eight
        and     r9d, -8
        cmp     r9d, r8d
        jbe     CC_SET
CC_GO_LEFT:
        mov     r9d, r8d                ;Margin at or just before it
        and     r9d, -8
CC_SET:
        cmp     r9d, MAX_MARGIN
        jbe     CC_SET2
        mov     r9d, MAX_MARGIN
CC_SET2:
        cmp     r9d, LEFT_MARGIN
        je      CC_VIEW
        mov     LEFT_MARGIN, r9d        ;Scroll, and redraw the screen
        or      DIRTY_BITS, 1
CC_VIEW:
        sub     r8d, LEFT_MARGIN        ;Column in the window
        jae     CC_IN
        xor     r8d, r8d
CC_IN:
        movzx   eax, COLUMNS
        dec     eax
        cmp     r8d, eax                ;Past end of display?
        jbe     COLUMN_OK
        mov     r8d, eax                ;Leave cursor at last column
COLUMN_OK:
        mov     edx, CUR_POSN
        mov     dx, r8w
        mov     CUR_POSN, edx           ;Store the row/column
        pop     rbx
        lea     rsp, [rbp]
        pop     rbp
        ret
CURSOR_COL      ENDP

;-----------------------------------------------------------------------
; This displays the UTF-8 string at RSI at the location in EDX.  The
; remainder of the row is erased.  Cursor is put at the end of the
; string, and EDX returns that position.
;-----------------------------------------------------------------------
TTY_STRING      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        call    POSITION                ;Compute offset into video
TTY_LOOP:
        cmp     byte ptr [rsi], 0       ;At end of string yet?
        je      TTY_DONE
        call    DECODE_STR
        cmp     dx, COLUMNS             ;Clip at the right edge
        jae     TTY_LOOP
        mov     ecx, edx
        movzx   r10d, INVERSE           ;Write in inverse video
        call    DRAW_GLYPH
        mov     dx, cx
        jmp     TTY_LOOP
TTY_DONE:
        call    SET_CURSOR              ;Move cursor to end of string
        lea     rsp, [rbp]
        pop     rbp
        jmp     ERASE_EOL               ;Erase the rest of line
TTY_STRING      ENDP

;-----------------------------------------------------------------------
; This copies the input filename to RDI and changes the extension to
; the 5-byte string (".XXX",0) at RSI.  Only a dot in the last path
; component counts as the extension.
;-----------------------------------------------------------------------
CHG_EXTENSION   PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rsi
        mov     rsi, NAME_POINTER
        xor     r8d, r8d                ;Position of the dot, if any
CHG_LOOP:
        lodsb
        test    al, al
        jz      CHG_END
        cmp     al, "."                 ;Look for the extension
        jne     CHG_NOT_DOT
        mov     r8, rdi
        jmp     CHG_STORE
CHG_NOT_DOT:
        cmp     al, "\"
        je      CHG_SLASH
        cmp     al, "/"
        jne     CHG_STORE
CHG_SLASH:
        xor     r8d, r8d                ;Dot was in a directory name
CHG_STORE:
        stosb                           ;Copy a character
        jmp     CHG_LOOP
CHG_END:
        test    r8, r8
        jz      CHG_APPEND
        mov     rdi, r8                 ;Overwrite the old extension
CHG_APPEND:
        mov     ecx, 5                  ;Five chars in extension
        pop     rsi
        rep     movsb                   ;Move new extension in
        lea     rsp, [rbp]
        pop     rbp
        ret
CHG_EXTENSION   ENDP

;=======================================================================
; Memory
;=======================================================================

;-----------------------------------------------------------------------
; Reserve address space for a buffer.  R8 = the buffer's descriptor.
; Tries RDX bytes first, then halves the size until a reservation
; succeeds (for systems that limit address space, such as programs
; run under a job object), stopping below MIN_RESERVE.  The size
; reserved is stored in the descriptor.  Returns the base in RAX
; (ZF=1 on failure).  No memory is committed yet.
;-----------------------------------------------------------------------
RESERVE_BUF     PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rbx
        mov     rbx, r8                 ;RBX = descriptor
        mov     [rbx+8], rdx
RB_TRY:
        xor     ecx, ecx
        mov     rdx, [rbx+8]
        mov     r8d, MEM_RESERVE
        mov     r9d, PAGE_NOACCESS
        CALLWIN VirtualAlloc
        test    rax, rax
        jnz     RB_DONE
        mov     rax, [rbx+8]            ;Didn't work - try half as much
        shr     rax, 1
        mov     [rbx+8], rax
        cmp     rax, MIN_RESERVE
        jae     RB_TRY
        xor     eax, eax                ;Give up
RB_DONE:
        pop     rbx
        test    rax, rax
        lea     rsp, [rbp]
        pop     rbp
        ret
RESERVE_BUF     ENDP

;-----------------------------------------------------------------------
; Make sure a reserved buffer has at least RDX + SLACK bytes committed.
; RCX = buffer base, RDX = bytes needed, R8 = the buffer's descriptor
; (bytes committed, bytes reserved).  Grows by about 1/8 at a time.
; Returns CF=1 if the memory isn't available or the buffer's
; reservation is too small.  All registers saved.
;-----------------------------------------------------------------------
COMMIT_BUF      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        PUSH_VOLATILE
        lea     r9, [rdx+SLACK]         ;Bytes that must be committed
        cmp     r9, [r8]                ;Already have that much?
        jbe     CB_OK
        mov     r10, r9
        shr     r10, 3
        add     r10, r9                 ;Add 1/8 for growing room
        add     r10, 0FFFFH
        and     r10, -10000H            ;Round up to 64K
        mov     rax, [r8+8]             ;Bytes reserved
        cmp     r9, rax                 ;Need more than that?
        ja      CB_FAIL
        cmp     r10, rax                ;Don't pass the reservation
        jbe     CB_SIZE_OK
        mov     r10, rax
CB_SIZE_OK:
        push    r8
        push    r10
        mov     rdx, r10                ;Commit from the base up
        mov     r8d, MEM_COMMIT
        mov     r9d, PAGE_READWRITE
        CALLWIN VirtualAlloc
        pop     r10
        pop     r8
        test    rax, rax
        jz      CB_FAIL
        mov     [r8], r10               ;New committed size
CB_OK:
        POP_VOLATILE
        clc
        lea     rsp, [rbp]
        pop     rbp
        ret
CB_FAIL:
        POP_VOLATILE
        stc
        lea     rsp, [rbp]
        pop     rbp
        ret
COMMIT_BUF      ENDP

;-----------------------------------------------------------------------
; Make sure the paste buffer can hold RCX bytes.  CF=1 if it can't.
;-----------------------------------------------------------------------
GROW_PASTE      PROC    FRAME
        push    rbp
        .pushreg rbp
        mov     rbp, rsp
        .setframe rbp, 0
        .endprolog
        push    rcx
        push    rdx
        push    r8
        mov     rdx, rcx
        mov     rcx, PASTE_SEG
        lea     r8, PASTE_COMMIT
        call    COMMIT_BUF
        pop     r8
        pop     rdx
        pop     rcx
        lea     rsp, [rbp]
        pop     rbp
        ret
GROW_PASTE      ENDP

;-----------------------------------------------------------------------
; Function key / cursor key dispatch table (scan codes 3Bh - 58h)
;-----------------------------------------------------------------------
.data
                ALIGN   8
DISPATCH_TABLE  DQ      ABORT    ,UNDO   ,COPY
                DQ      MARK     ,CUT    ,PASTE
                DQ      EXIT     ,DEL_EOL,DEL_L
                DQ      SAVE     ,BAD_KEY,BAD_KEY
                DQ      HOME     ,UP     ,PGUP
                DQ      BAD_KEY  ,LEFT   ,BAD_KEY
                DQ      RIGHT    ,BAD_KEY,ENDD
                DQ      DOWN     ,PGDN   ,INSERT
                DQ      DEL_CHAR ,BAD_KEY,BAD_KEY
                DQ      BAD_KEY  ,BAD_KEY               ;57h = F11 (unused)
                DQ      UDEL_L                          ;58h = F12

END
