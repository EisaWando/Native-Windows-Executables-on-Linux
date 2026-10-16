===================================================================
Linux Kernel Native Windows Executable Format Handler (binfmt_pe)
===================================================================

Overview
========

The ``binfmt_pe`` binary format handler enables native execution of 64-bit (x86_64)
and 32-bit (i386) Windows Portable Executable (PE/COFF) binaries directly inside
the Linux kernel space without external userspace translation layers.

Architecture & Components
=========================

1. **PE Binary Loader (fs/binfmt_pe.c)**:
   - Registers ``struct linux_binfmt pe_format`` with the kernel executable system.
   - Inspects file headers for DOS magic (``IMAGE_DOS_SIGNATURE`` / ``MZ``) and PE signature (``IMAGE_NT_SIGNATURE`` / ``PE\0\0``).
   - Parses COFF and Optional PE headers (``pe32_opt_hdr`` / ``pe32plus_opt_hdr``).
   - Maps executable sections (``.text``, ``.data``, ``.rdata``, ``.bss``) directly into process virtual memory using ``vm_mmap()``.
   - Configures process stack using ``setup_arg_pages()`` and transfers control to the entry point via ``start_thread()``.

2. **Windows NT Kernel Subsystem (kernel/sys_pe_nt.c & include/linux/pe_nt.h)**:
   - Provides native kernel-space implementations of Windows NT system calls.
   - Implements NT virtual memory primitives (``NtAllocateVirtualMemory``, ``NtFreeVirtualMemory``, ``NtProtectVirtualMemory``).
   - Implements NT file I/O operations (``NtCreateFile``, ``NtReadFile``, ``NtWriteFile``, ``NtClose``).
   - Implements native process management and system metrics (``NtTerminateProcess``, ``NtQuerySystemInformation``).

Kernel Configuration Options
============================

To enable native Windows PE support in kernel builds:

.. code-block:: text

   Executable file formats --->
       <*> Kernel support for native Windows PE binaries (CONFIG_BINFMT_PE)

Testing Native PE Execution
===========================

Once compiled into the kernel image, PE binaries can be launched directly:

.. code-block:: bash

   chmod +x sample_app.exe
   ./sample_app.exe

Kernel diagnostic output can be viewed via ``dmesg``:

.. code-block:: text

   [BINFMT_PE] Registered Native Windows PE Executable Format Handler
   [BINFMT_PE] Loading Windows PE Executable (64-bit x86_64, Base: 0x140000000, Entry: 0x140001000)
   [NT_SUBSYS] Native Windows NT Subsystem Execution Layer Initialized
