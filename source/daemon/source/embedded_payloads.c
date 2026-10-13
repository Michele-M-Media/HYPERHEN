#ifndef HYPERHEN_SHELLUI_ELF
#define HYPERHEN_SHELLUI_ELF "../assets/shellui.elf"
#endif
#ifndef HYPERHEN_UTIL_ELF
#define HYPERHEN_UTIL_ELF "../../bin/util.elf"
#endif
#ifndef HYPERHEN_ELFLDR_ELF
#define HYPERHEN_ELFLDR_ELF "../../bin/hyperhen_elfldr.elf"
#endif

 __asm__(


	".global shellui_elf_start\n"
	".type   shellui_elf_start, @object\n"
	".align  16\n"
	"shellui_elf_start:\n"
	    ".incbin \"" HYPERHEN_SHELLUI_ELF "\"\n"
	"shellui_elf_end:\n"
	    ".global shellui_elf_size\n"
	    ".type   shellui_elf_size, @object\n"
	    ".align  4\n"
	"shellui_prx_size:\n"
    	".int    shellui_elf_end - shellui_elf_start\n"

	/* Keep util in memory so the runtime supervisor can restart it without disk. */
	".global util_elf_start\n"
	".type   util_elf_start, @object\n"
	".align  16\n"
	"util_elf_start:\n"
		".incbin \"" HYPERHEN_UTIL_ELF "\"\n"
	"util_elf_end:\n"
		".global util_elf_size\n"
		".type   util_elf_size, @object\n"
		".align  4\n"
	"util_elf_size:\n"
		".int    util_elf_end - util_elf_start\n"

	/* Recovery root: daemon can relaunch private :9020 through external :9021. */
	".global hyperhen_elfldr_elf_start\n"
	".type   hyperhen_elfldr_elf_start, @object\n"
	".align  16\n"
	"hyperhen_elfldr_elf_start:\n"
		".incbin \"" HYPERHEN_ELFLDR_ELF "\"\n"
	"hyperhen_elfldr_elf_end:\n"
		".global hyperhen_elfldr_elf_size\n"
		".type   hyperhen_elfldr_elf_size, @object\n"
		".align  4\n"
	"hyperhen_elfldr_elf_size:\n"
		".int    hyperhen_elfldr_elf_end - hyperhen_elfldr_elf_start\n"

);
