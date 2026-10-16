// SPDX-License-Identifier: GPL-2.0-only
/*
 * Linux Kernel Native Windows PE (Portable Executable) Binary Format Handler
 * Allows native execution of Windows binaries directly inside Linux kernel space.
 * Pure PE/COFF loader: header parsing, section mapping, memory setup, entry point jump.
 * Win32 API dispatch is intentionally out of scope and must be handled in userland.
 * Educational and Testing Implementation
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/binfmts.h>
#include <linux/init.h>
#include <linux/file.h>
#include <linux/err.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/slab.h>
#include <linux/pe.h>
#include <asm/param.h>
#include <asm/processor.h>
#include <asm/msr.h>
#include <asm/gsseg.h>

#include <asm/segment.h>
#include <linux/auxvec.h>
#include <linux/string.h>

#ifndef PAGE_ALIGN_DOWN
#define PAGE_ALIGN_DOWN(addr) ((addr) & PAGE_MASK)
#endif
#ifndef PAGE_ALIGN_UP
#define PAGE_ALIGN_UP(addr)   PAGE_ALIGN(addr)
#endif

#define PE_STACK_SIZE		(8UL * 1024 * 1024)
#define PE_STACK_TOP		0x7ffff7000000UL
#define PE_STACK_SLACK		PAGE_SIZE

/*
 * The argument strings are already in the initial exec VMA at bprm->p.
 * Put the conventional ELF process-entry vector on the PE stack and retain
 * pointers to those strings.  The bridge currently ignores this vector, but
 * this keeps a replacement ELF-style _start ABI-correct.
 */
static int pe_build_initial_stack(struct linux_binprm *bprm,
				  unsigned long entry_point,
				  unsigned long user_sp,
				  unsigned long stack_top)
{
	unsigned long source = bprm->p;
	unsigned long arg_start = source, env_start;
	unsigned long *words;
	size_t word_count;
	int i, word = 0;

	if (bprm->argc < 0 || bprm->envc < 0)
		return -EINVAL;
	word_count = 1 + bprm->argc + 1 + bprm->envc + 1 + 6;
	if (word_count > (stack_top - user_sp) / sizeof(*words))
		return -E2BIG;

	words = kcalloc(word_count, sizeof(*words), GFP_KERNEL);
	if (!words)
		return -ENOMEM;

	words[word++] = bprm->argc;
	for (i = 0; i < bprm->argc; i++) {
		size_t len = strnlen_user((const char __user *)source,
					 MAX_ARG_STRLEN);

		if (!len || len > MAX_ARG_STRLEN)
			goto fault;
		words[word++] = source;
		source += len;
	}
	words[word++] = 0;
	current->mm->arg_start = arg_start;
	current->mm->arg_end = source;

	env_start = source;
	for (i = 0; i < bprm->envc; i++) {
		size_t len = strnlen_user((const char __user *)source,
					 MAX_ARG_STRLEN);

		if (!len || len > MAX_ARG_STRLEN)
			goto fault;
		words[word++] = source;
		source += len;
	}
	words[word++] = 0;
	current->mm->env_start = env_start;
	current->mm->env_end = source;

	words[word++] = AT_PAGESZ;
	words[word++] = PAGE_SIZE;
	words[word++] = AT_ENTRY;
	words[word++] = entry_point;
	words[word++] = AT_NULL;
	words[word++] = 0;

	if (copy_to_user((void __user *)user_sp, words,
			 word_count * sizeof(*words)))
		goto fault;

	bprm->p = user_sp;
	current->mm->start_stack = user_sp;
	kfree(words);
	return 0;

fault:
	kfree(words);
	return -EFAULT;
}

/*
 * do_arch_prctl_64(ARCH_SET_GS) is not exported to modules.  This is its
 * current-task state transition: GS has no selector and MSR_KERNEL_GS_BASE
 * holds the user GS base while the kernel is active after SWAPGS.
 */
static void pe_arch_set_gs(unsigned long gsbase)
{
	preempt_disable();
	load_gs_index(0);
	wrmsrq(MSR_KERNEL_GS_BASE, gsbase);
	current->thread.gsindex = 0;
	current->thread.gsbase = gsbase;
	preempt_enable();
}

static int load_pe_binary(struct linux_binprm *bprm)
{
	struct mz_hdr *dos_hdr;
	struct pe_hdr pe_header;
	struct pe32plus_opt_hdr opt64;
	struct pe32_opt_hdr opt32;
	unsigned long bridge_base;
	struct section_header *sections = NULL;
	unsigned long image_base, entry_point, image_size;
	unsigned long start_code = ~0UL, end_code = 0;
	unsigned long start_data = 0, end_data = 0;
	u32 import_rva = 0;
	u32 import_size = 0;
	u32 iat_rva = 0, iat_size = 0;
	u32 tls_rva = 0, tls_size = 0;
	u32 exception_rva = 0, exception_size = 0;
	u32 reloc_rva = 0, reloc_size = 0;
	u32 pe_header_size = 0;		/* SizeOfHeaders from optional hdr */
	bool is_64bit = false;
	loff_t pos;
	int retval = -ENOEXEC;
	unsigned long user_sp = 0;
	unsigned long stack_base = PE_STACK_TOP - PE_STACK_SIZE;
	unsigned long teb_vaddr = 0x7ff00000;
	unsigned long peb_vaddr = teb_vaddr + 0x800;
	int i;

	/* 1. Check MZ header signature */
	dos_hdr = (struct mz_hdr *)bprm->buf;
	if (dos_hdr->magic != IMAGE_DOS_SIGNATURE)
		return -ENOEXEC;

	if (dos_hdr->peaddr < sizeof(struct mz_hdr) || dos_hdr->peaddr > 0x1000)
		return -ENOEXEC;

	pr_info("[BINFMT_PE] Valid MS-DOS MZ Signature detected for file '%s'\n",
		bprm->filename);

	/* 2. Read PE header from binary */
	pos = dos_hdr->peaddr;
	retval = kernel_read(bprm->file, &pe_header, sizeof(pe_header), &pos);
	if (retval != sizeof(pe_header)) {
		pr_err("[BINFMT_PE] Failed to read PE header from '%s'\n", bprm->filename);
		return -ENOEXEC;
	}

	if (pe_header.magic != IMAGE_NT_SIGNATURE) {
		pr_err("[BINFMT_PE] Invalid PE Header Magic Signature 0x%x\n", pe_header.magic);
		return -ENOEXEC;
	}

	/* 3. Read Optional Header based on size/machine */
	if (pe_header.machine == IMAGE_FILE_MACHINE_AMD64) {
		is_64bit = true;
		retval = kernel_read(bprm->file, &opt64, sizeof(opt64), &pos);
		if (retval != sizeof(opt64))
			return -ENOEXEC;
		if (opt64.magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
			pr_err("[BINFMT_PE] Bad PE32+ optional magic 0x%x\n",
			       opt64.magic);
			return -ENOEXEC;
		}
		image_base = opt64.image_base;
		entry_point = image_base + opt64.entry_point;
		image_size = opt64.image_size;
		pe_header_size = opt64.header_size;

		/*
		 * PE32+ DataDirectory starts at OptionalHeader + 0x70.
		 * Each entry is a data_dirent (8 bytes: RVA + size).
		 * Validate index < NumberOfRvaAndSizes and that the
		 * optional header is large enough to contain the entry.
		 */
		{
			loff_t dd_base = dos_hdr->peaddr + sizeof(pe_header) + 0x70;
			u32 ndirs = opt64.data_dirs;
			struct data_dirent dd;

#define PE_READ_DATADIR(idx, rva_var, sz_var) do {			\
	if ((idx) < ndirs &&						\
	    pe_header.opt_hdr_size >= 0x70 + ((idx) + 1) *		\
	    sizeof(struct data_dirent)) {				\
		loff_t dpos = dd_base + (idx) * sizeof(struct data_dirent); \
		if (kernel_read(bprm->file, &dd, sizeof(dd),		\
				&dpos) == sizeof(dd)) {			\
			(rva_var) = dd.virtual_address;			\
			(sz_var) = dd.size;				\
		}							\
	}								\
} while (0)

			PE_READ_DATADIR(1,  import_rva,    import_size);
			PE_READ_DATADIR(3,  exception_rva, exception_size);
			PE_READ_DATADIR(5,  reloc_rva,     reloc_size);
			PE_READ_DATADIR(9,  tls_rva,       tls_size);
			PE_READ_DATADIR(12, iat_rva,       iat_size);
#undef PE_READ_DATADIR
		}
	} else if (pe_header.machine == IMAGE_FILE_MACHINE_I386) {
		is_64bit = false;
		retval = kernel_read(bprm->file, &opt32, sizeof(opt32), &pos);
		if (retval != sizeof(opt32))
			return -ENOEXEC;
		image_base = opt32.image_base;
		entry_point = image_base + opt32.entry_point;
		image_size = opt32.image_size;

		/* Read Data Directory 1 (Imports) */
		if (opt32.data_dirs > 1) {
			struct data_dirent import_dirent;
			pos = dos_hdr->peaddr + sizeof(pe_header) + sizeof(struct pe32_opt_hdr) + 1 * sizeof(struct data_dirent);
			if (kernel_read(bprm->file, &import_dirent, sizeof(import_dirent), &pos) == sizeof(import_dirent)) {
				import_rva = import_dirent.virtual_address;
				import_size = import_dirent.size;
			}
		}
	} else {
		pr_err("[BINFMT_PE] Unsupported PE machine arch 0x%x in '%s'\n",
		       pe_header.machine, bprm->filename);
		return -ENOEXEC;
	}

	if (pe_header.sections > 96) {
		pr_err("[BINFMT_PE] Too many sections (%u)\n", pe_header.sections);
		return -ENOEXEC;
	}

	if (import_rva && (import_rva >= image_size || import_size > image_size || import_rva + import_size > image_size)) {
		pr_err("[BINFMT_PE] Import directory out of bounds\n");
		return -ENOEXEC;
	}

	pr_info("[BINFMT_PE] Executable Parsing Success (%s, Base: 0x%lx, Entry: 0x%lx, Sections: %u, SizeOfHeaders: 0x%x)\n",
		is_64bit ? "64-bit x86_64" : "32-bit i386",
		image_base, entry_point, pe_header.sections, pe_header_size);
	pr_info("[BINFMT_PE]   Import    RVA: 0x%x  size: 0x%x\n", import_rva, import_size);
	pr_info("[BINFMT_PE]   IAT       RVA: 0x%x  size: 0x%x\n", iat_rva, iat_size);
	pr_info("[BINFMT_PE]   Exception RVA: 0x%x  size: 0x%x\n", exception_rva, exception_size);
	pr_info("[BINFMT_PE]   Reloc     RVA: 0x%x  size: 0x%x\n", reloc_rva, reloc_size);
	pr_info("[BINFMT_PE]   TLS       RVA: 0x%x  size: 0x%x\n", tls_rva, tls_size);

	/* Read section headers */
	if (pe_header.sections > 0) {
		size_t sec_size = pe_header.sections * sizeof(struct section_header);
		sections = kmalloc(sec_size, GFP_KERNEL);
		if (!sections)
			return -ENOMEM;

		pos = dos_hdr->peaddr + sizeof(pe_header) + pe_header.opt_hdr_size;
		retval = kernel_read(bprm->file, sections, sec_size, &pos);
		if (retval != sec_size) {
			kfree(sections);
			pr_err("[BINFMT_PE] Failed to read section headers\n");
			return -ENOEXEC;
		}
	}

	/* 4. Flush process memory and begin execution setup */
	retval = begin_new_exec(bprm);
	if (retval) {
		kfree(sections);
		return retval;
	}

	setup_new_exec(bprm);

	/* Preserve the DOS, NT, and optional headers at the PE image base. */
	{
		unsigned long hdr_map_size;
		unsigned long header_map;

		if (pe_header_size == 0 || pe_header_size > image_size) {
			pr_err("[BINFMT_PE] Invalid SizeOfHeaders 0x%x\n",
			       pe_header_size);
			kfree(sections);
			return -ENOEXEC;
		}
		hdr_map_size = PAGE_ALIGN(pe_header_size);

		header_map = vm_mmap(bprm->file, image_base, hdr_map_size,
				     PROT_READ,
				     MAP_PRIVATE | MAP_FIXED, 0);
		if (IS_ERR_VALUE(header_map)) {
			pr_err("[BINFMT_PE] Failed mapping PE headers at 0x%lx: %ld\n",
			       image_base, (long)header_map);
			kfree(sections);
			return (int)header_map;
		}
		pr_info("[BINFMT_PE] Mapped PE headers (%u bytes) read-only at 0x%lx\n",
			pe_header_size, image_base);
	}

	/* 4. Allocate an 8 MiB, downward-growing user stack. */
	{
		unsigned long stack_ret;

		stack_ret = vm_mmap(NULL, stack_base, PE_STACK_SIZE,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_STACK |
			MAP_GROWSDOWN, 0);
		if (IS_ERR_VALUE(stack_ret)) {
			pr_err("[BINFMT_PE] Failed to allocate stack space: %ld\n",
			       (long)stack_ret);
			return (int)stack_ret;
		}

		user_sp = ALIGN_DOWN(stack_base + PE_STACK_SIZE - PE_STACK_SLACK,
					     16);
		retval = pe_build_initial_stack(bprm, entry_point, user_sp,
						PE_STACK_TOP);
		if (retval)
			return retval;
	}

	/* 5. Map PE sections into user memory space */
	for (i = 0; i < pe_header.sections; i++) {
		struct section_header *sec = &sections[i];
		unsigned long vaddr = image_base + sec->virtual_address;
		unsigned long aligned_vaddr = PAGE_ALIGN_DOWN(vaddr);
		unsigned long offset_diff = vaddr - aligned_vaddr;
		unsigned long sec_len = sec->virtual_size ? sec->virtual_size : sec->raw_data_size;
		unsigned long aligned_size;
		unsigned long prot = PROT_READ;
		unsigned long map_addr;

		if (sec->raw_data_size > sec_len)
			sec_len = sec->raw_data_size;

		aligned_size = PAGE_ALIGN_UP(sec_len + offset_diff);

		if (sec->flags & IMAGE_SCN_MEM_WRITE)
			prot |= PROT_WRITE;
		if (sec->flags & IMAGE_SCN_MEM_EXECUTE)
			prot |= PROT_EXEC;

		if (aligned_size > 0) {
			map_addr = vm_mmap(NULL, aligned_vaddr, aligned_size,
					   prot | PROT_WRITE,
					   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0);
			if (IS_ERR_VALUE(map_addr)) {
				pr_err("[BINFMT_PE] Failed mapping section %.*s at 0x%lx (err: %ld)\n",
				       8, sec->name, aligned_vaddr, (long)map_addr);
			} else {
				if (sec->raw_data_size > 0 && sec->data_addr > 0) {
					size_t chunk_size = 65536;
					char *kbuf = kmalloc(chunk_size, GFP_KERNEL);
					if (kbuf) {
						size_t remaining = sec->raw_data_size;
						unsigned long dest_addr = vaddr;
						pos = sec->data_addr;
						while (remaining > 0) {
							size_t to_read = min(remaining, chunk_size);
							retval = kernel_read(bprm->file, kbuf, to_read, &pos);
							if (retval <= 0)
								break;
							if (copy_to_user((void __user *)dest_addr, kbuf, retval)) {
								pr_err("[BINFMT_PE] copy_to_user failed at 0x%lx for section %.*s\n",
								       dest_addr, 8, sec->name);
								break;
							}
							dest_addr += retval;
							remaining -= retval;
						}
						kfree(kbuf);
					}
				}
				pr_info("[BINFMT_PE] Mapped section %.*s at 0x%lx (vaddr: 0x%lx, size: 0x%lx, raw: 0x%x)\n",
					8, sec->name, aligned_vaddr, vaddr, aligned_size, sec->raw_data_size);
			}
		}

		if (sec->flags & IMAGE_SCN_CNT_CODE) {
			if (vaddr < start_code)
				start_code = vaddr;
			if (vaddr + sec_len > end_code)
				end_code = vaddr + sec_len;
		} else if (sec->flags & (IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_CNT_UNINITIALIZED_DATA)) {
			if (!start_data || vaddr < start_data)
				start_data = vaddr;
			if (vaddr + sec_len > end_data)
				end_data = vaddr + sec_len;
		}
	}

	kfree(sections);

	/* Set up process memory bounds */
	current->mm->start_code = start_code;
	current->mm->end_code = end_code;
	current->mm->start_data = start_data;
	current->mm->end_data = end_data;
	current->mm->start_brk = end_data;
	current->mm->brk = end_data + 0x100000;

	/* 5b. Initialize Windows TIB (Thread Information Block) and PEB (Process Environment Block) */
	{
		unsigned long map_addr = vm_mmap(NULL, teb_vaddr, PAGE_SIZE,
					 PROT_READ | PROT_WRITE,
					 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0);
		if (!IS_ERR_VALUE(map_addr)) {
			u64 tib_data[16] = {0};
			u64 peb_data[8] = {0};

			tib_data[1] = PE_STACK_TOP;
			tib_data[2] = stack_base;
			tib_data[6] = teb_vaddr; /* NT_TIB.Self, gs:[0x30] */
			tib_data[12] = peb_vaddr; /* TEB.ProcessEnvironmentBlock, gs:[0x60] */

			if (copy_to_user((void __user *)teb_vaddr, tib_data, sizeof(tib_data)) == 0) {
				peb_data[2] = image_base; /* PEB.ImageBaseAddress */
				if (copy_to_user((void __user *)peb_vaddr, peb_data, sizeof(peb_data)) == 0)
				pr_info("[BINFMT_PE] Initialized TEB (0x%lx) and PEB (0x%lx) for GS\n",
					teb_vaddr, peb_vaddr);
			}
		}
	}

	/* 5c. Map Heap Memory Page at 0x7ff30000 */
	{
		unsigned long heap_vaddr = 0x7ff30000;
		unsigned long map_addr = vm_mmap(NULL, heap_vaddr, 0x100000,
					 PROT_READ | PROT_WRITE,
					 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0);
		if (!IS_ERR_VALUE(map_addr)) {
			pr_info("[BINFMT_PE] Initialized Heap Memory Page at 0x%lx (1MB)\n", heap_vaddr);
		}
	}

	pr_info("[BINFMT_PE] Starting Native Execution at Entry Point 0x%lx for '%s'\n",
		entry_point, bprm->filename);

	/* 5e. Map win32_bridge.so safely */
	unsigned long bridge_entry = 0;
	{
		struct file *bridge_file;
		struct elfhdr elf_hdr;
		loff_t pos = 0;
		unsigned long bridge_map;
		loff_t bridge_size;
		char *bridge_buffer;
  bridge_file = filp_open("/home/eisa/Desktop/linux-7.1.6/win32_bridge.so", O_RDONLY, 0);
  if (!IS_ERR(bridge_file)) {
  	if (kernel_read(bridge_file, &elf_hdr, sizeof(elf_hdr), &pos) == sizeof(elf_hdr)) {
  		if (memcmp(elf_hdr.e_ident, ELFMAG, SELFMAG) == 0) {
  			bridge_base = 0x70000000;
			bridge_size = i_size_read(file_inode(bridge_file));
			if (bridge_size <= 0 || bridge_size > 0x100000) {
				pr_err("[BINFMT_PE] Invalid bridge size: %lld\n",
				       (long long)bridge_size);
				goto close_bridge;
			}
			/*
			 * The bridge is ET_DYN but no ELF dynamic linker runs here.  Copy
			 * its file image into anonymous memory so its .bss is zero-filled
			 * and writable rather than faulting past the backing file's EOF.
			 */
			bridge_map = vm_mmap(NULL, bridge_base, 0x100000,
					     PROT_READ | PROT_EXEC | PROT_WRITE,
					     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0);
			if (!IS_ERR_VALUE(bridge_map)) {
				bridge_buffer = kmalloc(65536, GFP_KERNEL);
				if (!bridge_buffer)
					goto close_bridge;
				pos = 0;
				while (pos < bridge_size) {
					size_t bytes = min_t(loff_t, 65536, bridge_size - pos);
					retval = kernel_read(bridge_file, bridge_buffer, bytes, &pos);
					if (retval != bytes ||
					    copy_to_user((void __user *)(bridge_base + pos - bytes),
							 bridge_buffer, bytes)) {
						pr_err("[BINFMT_PE] Failed copying bridge image\n");
						break;
					}
				}
				kfree(bridge_buffer);
				if (pos == bridge_size) {
					bridge_entry = bridge_base + elf_hdr.e_entry;
					pr_info("[BINFMT_PE] win32_bridge.so copied at 0x%lx (Entry: 0x%lx)\n",
						bridge_base, bridge_entry);
				}
  			}
  		}
  	}
close_bridge:
  	filp_close(bridge_file, NULL);
  }
}
/* 6. Execution handoff */
finalize_exec(bprm);

{
struct pt_regs *regs = task_pt_regs(current);
unsigned long target_ip = entry_point;

  if (bridge_entry != 0) {
  	target_ip = bridge_entry;
  }

	pr_info("[BINFMT_PE] Starting thread: stack_base=0x%lx user_sp=0x%lx\n",
		stack_base, user_sp);
	start_thread(regs, target_ip, user_sp);

	/* start_thread() clears the selector; install the user GS base afterwards. */
	pe_arch_set_gs(teb_vaddr);

  /* Force-override registers AFTER start_thread to prevent clobbering */
  regs->sp = user_sp;
  if (bridge_entry != 0) {
  	regs->di = entry_point;           /* Pass PE entry in RDI */
   regs->si = peb_vaddr;                 /* Pass PEB in RSI */
	regs->dx = image_base;              /* PE image base for bridge IAT fixups */
	regs->cx = import_rva;              /* IMAGE_DIRECTORY_ENTRY_IMPORT RVA */
	regs->r8 = import_size;             /* import directory byte size */
  }

  pr_info("[BINFMT_PE] Final handoff: IP=0x%lx SP=0x%lx RDI=0x%lx RSI=0x%lx RDX=0x%lx RCX=0x%lx R8=0x%lx\n",
  	regs->ip, regs->sp, regs->di, regs->si, regs->dx, regs->cx, regs->r8);
}

return 0;
}

static struct linux_binfmt pe_format = {
	.module		= THIS_MODULE,
	.load_binary	= load_pe_binary,
};

static int __init init_pe_binfmt(void)
{
	register_binfmt(&pe_format);
	pr_info("[BINFMT_PE] Registered Native Windows PE Executable Format Handler\n");
	return 0;
}

static void __exit exit_pe_binfmt(void)
{
	unregister_binfmt(&pe_format);
}

module_init(init_pe_binfmt);
module_exit(exit_pe_binfmt);

MODULE_DESCRIPTION("Linux Kernel Native Windows PE Binary Format Handler");
MODULE_LICENSE("GPL");
