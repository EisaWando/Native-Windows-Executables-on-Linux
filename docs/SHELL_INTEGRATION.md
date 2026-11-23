# Shell API Integration Guide for binfmt_pe

This document describes Windows Shell API integration points and their Linux equivalents, necessary for running Windows PE executables natively on Linux.

## Section 1: Linux File Systems

### File System Types
| File System | Case Sensitivity | Permissions/ACLs | Symlinks | Locking | Timestamp Resolution | Max Path | Max Name | Reserved Chars |
|-------------|------------------|-------------------|----------|---------|---------------------|----------|----------|----------------|
| ext4 | case-sensitive | POSIX mode, ACLs, xattrs | Full support | fcntl/flock | 1 ns | 4096 bytes | 255 bytes | /, \0 |
| btrfs | case-sensitive | POSIX mode, ACLs, xattrs | Full support | fcntl/flock | 1 ns | 4096 bytes | 255 bytes | /, \0 |
| xfs | case-sensitive | POSIX mode, ACLs | Full support | fcntl/flock | 1 ns | 4096 bytes | 255 bytes | /, \0 |
| tmpfs | case-sensitive | POSIX mode | Full support | fcntl/flock | 1 ns | 4096 bytes | 255 bytes | /, \0 |
| vfat/exfat | case-insensitive | Limited (chmod ignored) | Read-only | Advisory | 1s/10ms | 255 bytes | 255 bytes | /, *, ?, ", <, >, \| |
| ntfs3 | case-insensitive | POSIX mapping via xattr | Full | fcntl/flock | 100 ns | 4096 bytes | 255 bytes | /, :, *, ?, ", <, >, \| |
| SMB/NFS | case-insensitive (SMB) / case-sensitive (NFS) | Extended | Full | Advisory | 1s | 4096 bytes | 255 bytes | Varies |
| FUSE | depends on implementation | depends | depends | depends | depends | 4096 bytes | 255 bytes | Varies |

### Key Semantic Gaps
- **Path Length**: Linux paths capped at 4096 bytes; Windows supports 260-32767 with long path prefix
- **Case Sensitivity**: Linux is case-sensitive by default; Windows is case-insensitive
- **Reserved Characters**: Windows reserves `CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9` and characters `/:*?"<>|`
- **Symlink Support**: All major Linux filesystems support symlinks; Windows uses junctions, symlinks, and reparse points
- **Locking**: Linux uses advisory file locking; Windows uses mandatory file locking semantics

## Section 2: Windows-to-Linux Semantic Gaps for PE Runtime

### Drive Letters and Paths
- **Windows**: Drive letters (C:\, D:\) and UNC paths (\\server\share)
- **Linux**: Single unified filesystem with /mnt, /media for mounted devices
- **Mapping**: Use a virtual drive root (e.g., Z:\ maps to /); mount points under /media/user/<label> for removable media

### Case-Insensitive Lookup
- **Windows**: Case-insensitive, case-preserving
- **Linux**: Case-sensitive
- **Implementation**: Hash-based index with lowercase normalization, or linear scan with ascii_equal_ci()

### Case-Insensitive String Comparison
- Use custom function `ascii_equal_ci()` for DLL/function name matching
- Store function pointers in shim tables keyed by canonical lowercase names

### Reserved Names and Characters
- **Windows**: CON, PRN, AUX, NUL, COM1-COM9, LPT1-LPT9 are reserved
- **Linux**: No such restrictions
- **Implementation**: Reject or redirect access to reserved names in CreateFileW

### Share Modes vs Advisory Locks
- **Windows**: Share modes (FILE_SHARE_READ, FILE_SHARE_WRITE, FILE_SHARE_DELETE)
- **Linux**: fcntl() locks, flock(), or open() flags
- **Implementation**: Translate share mode flags to lock operations on file descriptor

### FILETIME
- **Windows**: 100-nanosecond intervals since January 1, 1601 (UTC)
- **Linux**: timespec (seconds + nanoseconds) since January 1, 1970 (UTC)
- **Conversion**: 
  ```c
  uint64_t filetime_to_unix_time(uint64_t ft) {
      return (ft / 10000000ULL) - 11644473600ULL;
  }
  uint64_t unix_time_to_filetime(uint64_t t) {
      return (t + 11644473600ULL) * 10000000ULL;
  }
  ```

### Long Paths
- **Windows**: MAX_PATH = 260; long paths require \\?\ prefix for up to 32767
- **Linux**: PATH_MAX = 4096 (or dynamically allocated)
- **Implementation**: Strip \\?\ prefix and pass through; no special handling needed for most cases

### Junctions and Symlinks
- **Windows**: NTFS reparse points for junctions and symlinks
- **Linux**: Native symlinks, bind mounts
- **Implementation**: CreateFileW with FILE_FLAG_OPEN_REPARSE_POINT reads reparse data; MapDirectoryJunction to symlink or bind mount

### Removable Media
- **Windows**: GetDriveType, GetVolumeInformation, drive letters
- **Linux**: /proc/mounts, /sys/block, udisks2 D-Bus API
- **Implementation**: Use statfs() and /proc/mounts to enumerate mount points; assign drive letters heuristically

### Recycle Bin vs XDG Trash
- **Windows**: Recycle Bin with recovery metadata (original name, path, deletion date)
- **Linux**: ~/.local/share/Trash (with info/ and files/ subdirectories)
- **Implementation**: SHFileOperation with FO_DELETE maps to gio trash or XDG trash-cli

## Section 3: Shell API Map

### ShellExecuteW/ExW

**Verbs Supported**: open, explore, edit, properties, runas, find, print, printto

| Verb | Meaning | Linux Backend | Notepad++ Import |
|------|---------|---------------|------------------|
| open | Open file | xdg-open | ShellExecuteW |
| explore | Open folder in explorer | xdg-open or xdg-dolphin --split | ShellExecuteW |
| edit | Edit file with associated editor | xdg-open | ShellExecuteW |
| properties | Show file properties | xdg-open with org.freedesktop.portal.FileDialog, or kioclient exec properties | ShellExecuteW |
| runas | Run with elevated privileges | pkexec xdg-open | ShellExecuteW |
| find | Search | xdg-open file:// for search | ShellExecuteW |
| print | Print file | lpr file | ShellExecuteW |
| printto | Print with printer name | lpr -P printer file | ShellExecuteW |

**Implementation Notes**:
- Parse verb and parameters from ShellExecuteW lpOperation, lpFile, lpParameters
- Map file types to MIME types using shared-mime-info database
- Use gio open for general purpose, fallback to xdg-open

### Explorer.exe /select

**Purpose**: Select and highlight a file in the file manager

| Windows | Linux Equivalent |
|---------|------------------|
| explorer.exe /select, "path" | xdg-open "file://path" or gio open "file://path" or dolphin --select "path" |

**Implementation**:
- Launch file manager with file:// URI
- Preferred: xdg-open (works with any registered file manager)
- Alternative: dolphin --select (KDE-specific)

### SHGetKnownFolderPath / SHGetFolderPath

**Purpose**: Get special folder paths (user directories, app data, etc.)

| Known Folder GUID | Meaning | Linux Equivalent |
|-------------------|---------|------------------|
| FOLDERID_Desktop | Desktop | ~/Desktop |
| FOLDERID_Documents | Documents | ~/Documents |
| FOLDERID_ProgramFiles | Program Files | /usr/bin (or /usr/local/bin) |
| FOLDERID_AppData | Application Data | ~/.config |
| FOLDERID_RoamingAppData | Roaming AppData | ~/.config |
| FOLDERID_LocalAppData | Local AppData | ~/.local/share |
| FOLDERID_Profile | User Profile | ~ |
| FOLDERID_Downloads | Downloads | ~/Downloads |
| FOLDERID_Video | Videos | ~/Videos |
| FOLDERID_Music | Music | ~/Music |
| FOLDERID_Pictures | Pictures | ~/Pictures |

**Implementation**: Use XDG Base Directory Specification variables and getpwnam() for user home.

### GetOpenFileName / IFileDialog

**Purpose**: File open dialog

| Windows | Linux Equivalent |
|---------|------------------|
| GetOpenFileName | GIO portal file dialog (org.freedesktop.portal.FileChooser) |
| GetSaveFileName | GIO portal file dialog |
| IFileDialog | GIO portal file dialog |

**Implementation via GIO Portal**:
- Use xdg-dbus-proxy or direct D-Bus call to org.freedesktop.portal.FileChooser
- Falls back to zenity/kdialog if portal unavailable
- Maps to native file manager dialog

**Notepad++ Imports**: GetOpenFileName, GetSaveFileName, IFileDialog variants

### SHFileOperation

**Purpose**: File operations (copy, move, delete, rename, make dir)

| Operation | Linux Command |
|-----------|---------------|
| FO_COPY | cp -a source dest thengio mount --no-restore-source dest |
| FO_MOVE | mv source dest |
| FO_DELETE | gio trash source or rm source |
| FO_RENAME | mv old new |
| FO_CREATEFOLDER | mkdir -p path |

**Implementation**:
- Map fsf operations to shell commands via fork+exec
- Use gio trash for FO_DELETE to move to trash instead of permanent delete

**Notepad++ Imports**: SHFileOperation, SHCopyFile, SHMoveFile, SHDeleteFile

### SHChangeNotify

**Purpose**: Notify shell of file system changes

| Event | Meaning | Linux Equivalent |
|-------|---------|------------------|
| SHCNE_CREATE | Item created | Create inotify watch |
| SHCNE_DELETE | Item deleted | inotify WM_DELETE |
| SHCNE_MKDIR | Directory created | inotify IN_CREATE with ISDIR |
| SHCNE_RMDIR | Directory deleted | inotify IN_DELETE with ISDIR |
| SHCNE_RENAME | Item renamed | inotify IN_MOVED_FROM/TO |

**Implementation**: Use inotify for real-time notifications, or send GIO notifications via D-Bus.

### File Associations

**Purpose**: Determine default program for file type

| Windows | Linux |
|---------|-------|
| RegQueryValueEx(HKCR\.ext) | shared-mime-info get-mime-logical /path/to/file |
| ShellEx\OpenCommand | xdg-mime query default text/plain |
| ProgID | xdg-mime query filetype /path/to/file |

**Implementation**:
- Parse /usr/share/mime/packages/*.xml and ~/.local/share/mime/packages/
- Build MIME hash table for fast extension lookup
- Use xdg-mime for user overrides

## Section 4: Phased Plan (ShellExecuteW Focus)

### Phase 0: Foundation (COMPLETED)
- Basic Win32 API shims (CreateFile, ReadFile, WriteFile, CloseHandle)
- Heap management (HeapCreate, HeapAlloc, HeapFree)
- Process creation (CreateProcessW)
- Thread management
- Sync primitives (CRITICAL_SECTION, events, mutexes)
- Time functions (GetSystemTime, SetSystemTime)
- Debug output (OutputDebugString)

### Phase 1: Tracing Infrastructure (IN PROGRESS)
- BRIDGE_TRACE_MODULES environment variable
- Trace LoadLibraryExW, GetModuleHandle, GetProcAddress calls
- Identify missing modules (user32.dll, gdi32.dll)
- Verify module registration in builtin_module_names[]

### Phase 2: Core GUI (NEXT)
- user32.dll functions: CreateWindowExW, GetMessageW, PeekMessageW, DispatchMessageW
- gdi32.dll functions: CreateCompatibleDC, SelectObject, BitBlt, DeleteDC, DeleteObject
- ShowWindow, UpdateWindow, InvalidateRect
- Message loop infrastructure

### Phase 3: File Dialog Integration (FUTURE)
- Implement GetOpenFileNameW / GetSaveFileNameW
- Integrate GIO portal for cross-desktop compatibility
- Implement SHFileOperation for file operations

### Phase 4: ShellExecuteW (FUTURE)
- Implement ShellExecuteW and ShellExecuteExW
- Map verbs to appropriate Linux commands
- Handle file type associations via shared-mime-info
- Support runas verb with pkexec

### Phase 5: Full Explorer Support (FAR FUTURE)
- Directory monitoring with inotify
- SHChangeNotify implementation
- Right-click context menu support
- Drag and drop integration