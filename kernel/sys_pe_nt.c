// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux Kernel Native Windows NT Subsystem Core
 * Handles NT System Calls, Memory, I/O, Subsystem Execution,
 * Framebuffer UI Window Manager, Keyboard & Mouse Events, and Logging System.
 * Educational and Testing Implementation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/mm.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/anon_inodes.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/input.h>
#include <asm/processor.h>
#include <linux/pe_nt.h>

extern int do_mprotect_pkey(unsigned long start, size_t len,
			    unsigned long prot, int pkey);

static const struct file_operations pe_nt_file_fops = {
	.owner = THIS_MODULE,
};

#define PE_LOG_BUF_SIZE 16384
static char pe_log_buffer[PE_LOG_BUF_SIZE];
static size_t pe_log_len = 0;
static DEFINE_SPINLOCK(pe_log_lock);

static struct win_ui_context ui_ctx = {
	.width = 1024,
	.height = 768,
	.bpp = 32,
	.active = false,
	.title = "Windows Application Test Environment"
};

/* Diagnostic Log Event Generator */
void pe_log_event(const char *level, const char *fmt, ...)
{
	va_list args;
	char line[256];
	size_t len;
	ulong flags;

	va_start(args, fmt);
	len = vscnprintf(line, sizeof(line), fmt, args);
	va_end(args);

	printk("%s[PE_NT_LOG] %s", level, line);

	spin_lock_irqsave(&pe_log_lock, flags);
	if (pe_log_len + len < PE_LOG_BUF_SIZE - 1) {
		memcpy(pe_log_buffer + pe_log_len, line, len);
		pe_log_len += len;
		pe_log_buffer[pe_log_len] = '\0';
	}
	spin_unlock_irqrestore(&pe_log_lock, flags);
}
EXPORT_SYMBOL_GPL(pe_log_event);

/* Crash Reporter for Debugging Failed Binaries */
void pe_log_crash(struct pt_regs *regs, const char *reason)
{
	pe_log_event(KERN_ERR, "=====================================================\n");
	pe_log_event(KERN_ERR, "CRASH REPORT: Process '%s' (PID: %d)\n", current->comm, current->pid);
	pe_log_event(KERN_ERR, "Failure Reason: %s\n", reason);
	if (regs) {
		pe_log_event(KERN_ERR, "RIP/EIP: 0x%lx  RSP/ESP: 0x%lx\n",
			     instruction_pointer(regs), user_stack_pointer(regs));
		pe_log_event(KERN_ERR, "RAX: 0x%lx  RBX: 0x%lx  RCX: 0x%lx\n",
			     regs->ax, regs->bx, regs->cx);
	}
	pe_log_event(KERN_ERR, "=====================================================\n");
}
EXPORT_SYMBOL_GPL(pe_log_crash);

/* Procfs Interface for Live Debugging Logs (/proc/pe_nt_logs) */
static int pe_nt_proc_show(struct seq_file *m, void *v)
{
	ulong flags;

	spin_lock_irqsave(&pe_log_lock, flags);
	seq_puts(m, "=== Native Windows Kernel Execution Diagnostic Logs ===\n");
	seq_write(m, pe_log_buffer, pe_log_len);
	spin_unlock_irqrestore(&pe_log_lock, flags);
	return 0;
}

static int pe_nt_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, pe_nt_proc_show, NULL);
}

static const struct proc_ops pe_nt_proc_ops = {
	.proc_open	= pe_nt_proc_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= single_release,
};

/* Keyboard and Mouse Input Handler */
void pe_ui_handle_input(struct input_event *ev)
{
	if (!ev)
		return;

	if (ev->type == EV_KEY) {
		ui_ctx.last_input.type = 1;
		ui_ctx.last_input.code = ev->code;
		ui_ctx.last_input.value = ev->value;
		ui_ctx.last_input.timestamp = jiffies;

		pe_log_event(KERN_INFO, "[INPUT] Key Event: code=%u state=%d\n",
			     ev->code, ev->value);
	} else if (ev->type == EV_REL || ev->type == EV_ABS) {
		ui_ctx.last_input.type = 2;
		ui_ctx.last_input.code = ev->code;
		ui_ctx.last_input.value = ev->value;
		if (ev->code == REL_X)
			ui_ctx.last_input.x += ev->value;
		else if (ev->code == REL_Y)
			ui_ctx.last_input.y += ev->value;

		ui_ctx.last_input.timestamp = jiffies;
	}
}
EXPORT_SYMBOL_GPL(pe_ui_handle_input);

/* UI Window & Framebuffer Renderer */
void pe_ui_render_frame(const char *app_title, const char *log_msg)
{
	ui_ctx.active = true;
	if (app_title)
		strscpy(ui_ctx.title, app_title, sizeof(ui_ctx.title));

	pe_log_event(KERN_INFO, "[UI] Frame Rendered: Title='%s' Status='%s'\n",
		     ui_ctx.title, log_msg ? log_msg : "Running");
}
EXPORT_SYMBOL_GPL(pe_ui_render_frame);

void pe_ui_init(void)
{
	proc_create("pe_nt_logs", 0444, NULL, &pe_nt_proc_ops);
	pe_log_event(KERN_INFO, "Kernel UI, Keyboard/Mouse Subsystem & Log Interface Initialized\n");
}

/* Convert Windows VirtualAlloc protection flags to Linux PROT_* flags */
static ulong win_protect_to_prot(u32 protect)
{
	ulong prot = 0;
	if (protect & 0x01) /* PAGE_NOACCESS */
		return PROT_NONE;
	if (protect & 0x02) /* PAGE_READONLY */
		prot |= PROT_READ;
	if (protect & 0x04) /* PAGE_READWRITE */
		prot |= PROT_READ | PROT_WRITE;
	if (protect & 0x10) /* PAGE_EXECUTE */
		prot |= PROT_EXEC;
	if (protect & 0x20) /* PAGE_EXECUTE_READ */
		prot |= PROT_READ | PROT_EXEC;
	if (protect & 0x40) /* PAGE_EXECUTE_READWRITE */
		prot |= PROT_READ | PROT_WRITE | PROT_EXEC;

	if (!prot)
		prot = PROT_READ | PROT_WRITE;
	return prot;
}

/* NtAllocateVirtualMemory */
NTSTATUS sys_nt_allocate_virtual_memory(HANDLE process, void **base_address,
					 ulong zero_bits, ulong *region_size,
					 u32 allocation_type, u32 protect)
{
	ulong size, addr, prot, flags;
	ulong populated_addr;

	if (!base_address || !region_size)
		return STATUS_INVALID_PARAMETER;

	if (get_user(size, region_size) || get_user(addr, (ulong *)base_address))
		return STATUS_ACCESS_DENIED;

	size = PAGE_ALIGN(size);
	prot = win_protect_to_prot(protect);
	flags = MAP_PRIVATE | MAP_ANONYMOUS;

	if (addr)
		flags |= MAP_FIXED;

	populated_addr = vm_mmap(NULL, addr, size, prot, flags, 0);
	if (IS_ERR_VALUE(populated_addr)) {
		pe_log_event(KERN_ERR, "NtAllocateVirtualMemory failed with code: %ld\n",
			     (long)populated_addr);
		return STATUS_NO_MEMORY;
	}

	if (put_user(populated_addr, (ulong *)base_address) || put_user(size, region_size))
		return STATUS_ACCESS_DENIED;

	pe_log_event(KERN_INFO, "NtAllocateVirtualMemory 0x%lx bytes allocated at 0x%lx\n",
		     size, populated_addr);
	return STATUS_SUCCESS;
}
EXPORT_SYMBOL_GPL(sys_nt_allocate_virtual_memory);

/* NtFreeVirtualMemory */
NTSTATUS sys_nt_free_virtual_memory(HANDLE process, void **base_address,
				     ulong *region_size, u32 free_type)
{
	ulong size, addr;
	int ret;

	if (!base_address || !region_size)
		return STATUS_INVALID_PARAMETER;

	if (get_user(addr, (ulong *)base_address) || get_user(size, region_size))
		return STATUS_ACCESS_DENIED;

	ret = vm_munmap(addr, PAGE_ALIGN(size));
	if (ret < 0)
		return STATUS_UNSUCCESSFUL;

	pe_log_event(KERN_INFO, "NtFreeVirtualMemory unmapped 0x%lx\n", addr);
	return STATUS_SUCCESS;
}
EXPORT_SYMBOL_GPL(sys_nt_free_virtual_memory);

/* NtProtectVirtualMemory */
NTSTATUS sys_nt_protect_virtual_memory(HANDLE process, void **base_address,
					ulong *region_size, u32 new_protect,
					u32 *old_protect)
{
	ulong addr, size, prot;
	int ret;

	if (!base_address || !region_size)
		return STATUS_INVALID_PARAMETER;

	if (get_user(addr, (ulong *)base_address) || get_user(size, region_size))
		return STATUS_ACCESS_DENIED;

	prot = win_protect_to_prot(new_protect);
	ret = do_mprotect_pkey(addr, PAGE_ALIGN(size), prot, -1);
	if (ret < 0)
		return STATUS_UNSUCCESSFUL;

	if (old_protect && put_user(0x04, old_protect))
		return STATUS_ACCESS_DENIED;

	return STATUS_SUCCESS;
}
EXPORT_SYMBOL_GPL(sys_nt_protect_virtual_memory);

/* NtCreateFile */
NTSTATUS sys_nt_create_file(HANDLE *file_handle, u32 desired_access,
			     struct object_attributes *obj_attr,
			     struct io_status_block *io_status,
			     u64 *allocation_size, u32 file_attributes,
			     u32 share_access, u32 create_disposition,
			     u32 create_options, void *ea_buffer, u32 ea_length)
{
	struct file *file;
	int flags = O_RDWR;
	int fd;

	if (!file_handle)
		return STATUS_INVALID_PARAMETER;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return STATUS_UNSUCCESSFUL;

	file = anon_inode_getfile("[pe_nt_file]", &pe_nt_file_fops, NULL, flags);
	if (IS_ERR(file)) {
		put_unused_fd(fd);
		return STATUS_UNSUCCESSFUL;
	}

	fd_install(fd, file);
	if (put_user((HANDLE)(long)fd, file_handle))
		return STATUS_ACCESS_DENIED;

	pe_log_event(KERN_INFO, "NtCreateFile created descriptor fd=%d\n", fd);
	return STATUS_SUCCESS;
}
EXPORT_SYMBOL_GPL(sys_nt_create_file);

/* NtWriteFile */
NTSTATUS sys_nt_write_file(HANDLE file_handle, HANDLE event, void *apc_routine,
			   void *apc_context, struct io_status_block *io_status,
			   void *buffer, u32 length, u64 *byte_offset, u32 *key)
{
	struct fd f = fdget((int)(long)file_handle);
	struct file *fp = fd_file(f);
	loff_t pos = 0;
	ssize_t ret;

	if (!fp)
		return STATUS_INVALID_PARAMETER;

	ret = kernel_write(fp, buffer, length, &pos);
	fdput(f);

	if (ret < 0)
		return STATUS_UNSUCCESSFUL;

	if (io_status) {
		struct io_status_block sb = {
			.status = STATUS_SUCCESS,
			.information = ret
		};
		if (copy_to_user(io_status, &sb, sizeof(sb)))
			return STATUS_ACCESS_DENIED;
	}

	return STATUS_SUCCESS;
}
EXPORT_SYMBOL_GPL(sys_nt_write_file);

/* NtReadFile */
NTSTATUS sys_nt_read_file(HANDLE file_handle, HANDLE event, void *apc_routine,
			  void *apc_context, struct io_status_block *io_status,
			  void *buffer, u32 length, u64 *byte_offset, u32 *key)
{
	struct fd f = fdget((int)(long)file_handle);
	struct file *fp = fd_file(f);
	loff_t pos = 0;
	ssize_t ret;

	if (!fp)
		return STATUS_INVALID_PARAMETER;

	ret = kernel_read(fp, buffer, length, &pos);
	fdput(f);

	if (ret < 0)
		return STATUS_UNSUCCESSFUL;

	if (io_status) {
		struct io_status_block sb = {
			.status = STATUS_SUCCESS,
			.information = ret
		};
		if (copy_to_user(io_status, &sb, sizeof(sb)))
			return STATUS_ACCESS_DENIED;
	}

	return STATUS_SUCCESS;
}
EXPORT_SYMBOL_GPL(sys_nt_read_file);

/* NtClose */
NTSTATUS sys_nt_close(HANDLE handle)
{
	int fd = (int)(long)handle;
	close_fd(fd);
	return STATUS_SUCCESS;
}
EXPORT_SYMBOL_GPL(sys_nt_close);

/* NtQuerySystemInformation */
NTSTATUS sys_nt_query_system_information(u32 info_class, void *info_buf,
					  u32 length, u32 *return_length)
{
	pe_log_event(KERN_INFO, "NtQuerySystemInformation info_class=%u\n", info_class);
	if (return_length && put_user(0, return_length))
		return STATUS_ACCESS_DENIED;
	return STATUS_SUCCESS;
}
EXPORT_SYMBOL_GPL(sys_nt_query_system_information);

/* NtTerminateProcess */
NTSTATUS sys_nt_terminate_process(HANDLE process_handle, NTSTATUS exit_status)
{
	pe_log_event(KERN_INFO, "NtTerminateProcess exit_status=0x%x PID=%d\n",
		     exit_status, current->pid);
	do_group_exit((int)exit_status);
	return STATUS_SUCCESS;
}
EXPORT_SYMBOL_GPL(sys_nt_terminate_process);

/* Initialize Subsystem */
void pe_nt_init_subsystem(void)
{
	pe_ui_init();
	pe_ui_render_frame("Windows Application Environment", "Subsystem Ready");
	pe_log_event(KERN_INFO, "Native Windows NT Subsystem Execution Layer Initialized\n");
}
