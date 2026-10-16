/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux Kernel Native Windows NT Subsystem Interface
 * Educational and Testing Implementation for Native Windows App Execution
 * Includes Framebuffer UI, Keyboard/Mouse Event Handlers, and Diagnostic Logger
 */

#ifndef _LINUX_PE_NT_H
#define _LINUX_PE_NT_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/input.h>

/* NT Status Codes */
typedef u32 NTSTATUS;
#define STATUS_SUCCESS                   ((NTSTATUS)0x00000000L)
#define STATUS_UNSUCCESSFUL              ((NTSTATUS)0xC0000001L)
#define STATUS_NOT_IMPLEMENTED           ((NTSTATUS)0xC0000002L)
#define STATUS_INVALID_INFO_CLASS        ((NTSTATUS)0xC0000003L)
#define STATUS_INFO_LENGTH_MISMATCH      ((NTSTATUS)0xC0000004L)
#define STATUS_ACCESS_DENIED             ((NTSTATUS)0xC0000022L)
#define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023L)
#define STATUS_OBJECT_NAME_NOT_FOUND     ((NTSTATUS)0xC0000034L)
#define STATUS_INVALID_PARAMETER         ((NTSTATUS)0xC000000DL)
#define STATUS_NO_MEMORY                 ((NTSTATUS)0xC0000017L)
#define STATUS_SECTION_TOO_BIG           ((NTSTATUS)0xC0000040L)

/* Windows Native Handle Definition */
typedef void *HANDLE;
#define INVALID_HANDLE_VALUE             ((HANDLE)(long)-1)

/* NT Unicode String */
struct unicode_string {
	u16 length;
	u16 maximum_length;
	u16 *buffer;
};

/* NT Object Attributes */
struct object_attributes {
	u32 length;
	HANDLE root_directory;
	struct unicode_string *object_name;
	u32 attributes;
	void *security_descriptor;
	void *security_quality_of_service;
};

/* NT IO Status Block */
struct io_status_block {
	union {
		NTSTATUS status;
		void *pointer;
	};
	ulong information;
};

/* Win32 Process Environment Block (PEB) layout */
struct win_peb {
	u8 inherited_address_space;
	u8 read_image_file_exec_options;
	u8 being_debugged;
	u8 bit_field;
	void *mutant;
	void *image_base_address;
	void *ldr;
	void *process_parameters;
	void *subsystem_data;
	void *process_heap;
};

/* Win32 Thread Environment Block (TEB) layout */
struct win_teb {
	void *exception_list;
	void *stack_base;
	void *stack_limit;
	void *subsystem_tib;
	void *fiber_data;
	void *arbitrary_user_pointer;
	struct win_teb *self;
	void *environment_pointer;
	u32 process_id;
	u32 thread_id;
	void *active_rpc_handle;
	void *thread_local_storage_pointer;
	struct win_peb *process_environment_block;
};

/* Input Event Data Structure for Keyboard & Mouse */
struct win_input_event {
	u32 type;       /* 1 = Key, 2 = Mouse Move, 3 = Mouse Click */
	u32 code;       /* Keycode or Mouse Button */
	s32 value;      /* Key State or Relative Offset */
	s32 x;          /* Mouse X Position */
	s32 y;          /* Mouse Y Position */
	u64 timestamp;  /* Timestamp in jiffies */
};

/* Kernel UI Window Manager Context */
struct win_ui_context {
	u32 width;
	u32 height;
	u32 bpp;
	u32 *framebuffer;
	bool active;
	char title[64];
	struct win_input_event last_input;
};

/* Kernel NT System Call Interface Functions */
NTSTATUS sys_nt_allocate_virtual_memory(HANDLE process, void **base_address,
					 ulong zero_bits, ulong *region_size,
					 u32 allocation_type, u32 protect);

NTSTATUS sys_nt_free_virtual_memory(HANDLE process, void **base_address,
				     ulong *region_size, u32 free_type);

NTSTATUS sys_nt_protect_virtual_memory(HANDLE process, void **base_address,
					ulong *region_size, u32 new_protect,
					u32 *old_protect);

NTSTATUS sys_nt_create_file(HANDLE *file_handle, u32 desired_access,
			     struct object_attributes *obj_attr,
			     struct io_status_block *io_status,
			     u64 *allocation_size, u32 file_attributes,
			     u32 share_access, u32 create_disposition,
			     u32 create_options, void *ea_buffer, u32 ea_length);

NTSTATUS sys_nt_read_file(HANDLE file_handle, HANDLE event, void *apc_routine,
			  void *apc_context, struct io_status_block *io_status,
			  void *buffer, u32 length, u64 *byte_offset, u32 *key);

NTSTATUS sys_nt_write_file(HANDLE file_handle, HANDLE event, void *apc_routine,
			   void *apc_context, struct io_status_block *io_status,
			   void *buffer, u32 length, u64 *byte_offset, u32 *key);

NTSTATUS sys_nt_close(HANDLE handle);

NTSTATUS sys_nt_terminate_process(HANDLE process_handle, NTSTATUS exit_status);

NTSTATUS sys_nt_query_system_information(u32 info_class, void *info_buf,
					  u32 length, u32 *return_length);

NTSTATUS sys_nt_display_string(struct unicode_string *string);

/* Input and UI Management API */
void pe_ui_init(void);
void pe_ui_handle_input(struct input_event *ev);
void pe_ui_render_frame(const char *app_title, const char *log_msg);

/* Kernel Diagnostic Logging System API */
void pe_log_event(const char *level, const char *fmt, ...);
void pe_log_crash(struct pt_regs *regs, const char *reason);

void pe_nt_init_subsystem(void);

#endif /* _LINUX_PE_NT_H */
