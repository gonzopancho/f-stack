#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/refcount.h>
#include <sys/stat.h>
#include <sys/stdint.h>
#include <sys/time.h>
#include <sys/ucred.h>
#include <sys/uio.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/sx.h>
#include <sys/linker.h>
#include <sys/racct.h>
#include <sys/malloc.h>
#include <sys/syscallsubr.h>
#include <sys/libkern.h>
#include <sys/random.h>
#include <sys/mman.h>
#include <sys/vdso.h>

#include <machine/elf.h>
#include <machine/md_var.h>

#include "ff_host_interface.h"

TAILQ_HEAD(prisonlist, prison);

__thread struct thread *pcurthread;

struct cdev;
struct vnode *rootvnode;
extern struct proc proc0;
struct proclist allproc;
struct sx allproc_lock;
struct sx allprison_lock;
struct prisonlist allprison;

MALLOC_DEFINE(M_FADVISE, "fadvise", "posix_fadvise(2) information");
int async_io_version;

#define M_ZERO        0x0100        /* bzero the allocation */

int vttoif_tab[10] = {
    0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK,
    S_IFSOCK, S_IFIFO, S_IFMT, S_IFMT
};

void ff_init_thread0(void);

void
resettodr(void)
{

}

void
ff_init_thread0(void)
{
    pcurthread = &thread0;
}

int
kproc_kthread_add(void (*start_routine)(void *), void *arg,
    struct proc **p,  struct thread **tdp,
    int flags, int pages,
    const char *procname, const char *str, ...)
{
    return 0;
}

int
kthread_add(void (*start_routine)(void *), void *arg, struct proc *p,
    struct thread **tdp, int flags, int pages,
    const char *str, ...)
{
    return 0;
}

void
kthread_exit(void)
{
    panic("kthread_exit unsupported");
}

void
tdsignal(struct thread *td, int sig)
{
    return;
}

dev_t
tty_udev(struct tty *tp)
{
    return (NODEV);
}

int
p_candebug(struct thread *td, struct proc *p)
{
    return (0);
}

const char *
devtoname(struct cdev *dev)
{
    return (NULL);
}

#ifdef RACCT
uint64_t
racct_get_limit(struct proc *p, int resource)
{
    return (UINT64_MAX);
}
#endif

int
kern_openat(struct thread *td, int fd, char *path, enum uio_seg pathseg,
    int flags, int mode)
{
    return (-1);
}

/* Process one elf relocation with addend. */
static int
elf_reloc_internal(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, int local, elf_lookup_fn lookup)
{
    Elf64_Addr *where, val;
    Elf32_Addr *where32, val32;
    Elf_Addr addr;
    Elf_Addr addend;
    Elf_Size rtype, symidx;
    const Elf_Rel *rel;
    const Elf_Rela *rela;
    int error;

    switch (type) {
        case ELF_RELOC_REL:
            rel = (const Elf_Rel *)data;
            where = (Elf_Addr *) (relocbase + rel->r_offset);
            rtype = ELF_R_TYPE(rel->r_info);
            symidx = ELF_R_SYM(rel->r_info);
            /* Addend is 32 bit on 32 bit relocs */
            switch (rtype) {
                case R_X86_64_PC32:
                case R_X86_64_32S:
                    addend = *(Elf32_Addr *)where;
                    break;
                default:
                    addend = *where;
                    break;
            }
            break;
        case ELF_RELOC_RELA:
            rela = (const Elf_Rela *)data;
            where = (Elf_Addr *) (relocbase + rela->r_offset);
            addend = rela->r_addend;
            rtype = ELF_R_TYPE(rela->r_info);
            symidx = ELF_R_SYM(rela->r_info);
            break;
        default:
            panic("unknown reloc type %d\n", type);
    }

    switch (rtype) {
        case R_X86_64_NONE:    /* none */
            break;

        case R_X86_64_64:        /* S + A */
            error = lookup(lf, symidx, 1, &addr);
            val = addr + addend;
            if (error != 0)
                return -1;
            if (*where != val)
                *where = val;
            break;

        case R_X86_64_PC32:    /* S + A - P */
            error = lookup(lf, symidx, 1, &addr);
            where32 = (Elf32_Addr *)where;
            val32 = (Elf32_Addr)(addr + addend - (Elf_Addr)where);
            if (error != 0)
                return -1;
            if (*where32 != val32)
                *where32 = val32;
            break;

        case R_X86_64_32S:    /* S + A sign extend */
            error = lookup(lf, symidx, 1, &addr);
            val32 = (Elf32_Addr)(addr + addend);
            where32 = (Elf32_Addr *)where;
            if (error != 0)
                return -1;
            if (*where32 != val32)
                *where32 = val32;
            break;

        case R_X86_64_COPY:    /* none */
            /*
             * There shouldn't be copy relocations in kernel
             * objects.
             */
            printf("kldload: unexpected R_COPY relocation\n");
            return -1;
            break;

        case R_X86_64_GLOB_DAT:    /* S */
        case R_X86_64_JMP_SLOT:    /* XXX need addend + offset */
            error = lookup(lf, symidx, 1, &addr);
            if (error != 0)
                return -1;
            if (*where != addr)
                *where = addr;
            break;

        case R_X86_64_RELATIVE:    /* B + A */
            addr = relocbase + addend;
            val = addr;
            if (*where != val)
                *where = val;
            break;

        default:
            printf("kldload: unexpected relocation type %ld\n",
                   rtype);
            return -1;
    }
    return(0);
}

int
elf_reloc(linker_file_t lf, Elf_Addr relocbase, const void *data, int type,
    elf_lookup_fn lookup)
{
    return (elf_reloc_internal(lf, relocbase, data, type, 0, lookup));
}

int
elf_reloc_local(linker_file_t lf, Elf_Addr relocbase, const void *data,
    int type, elf_lookup_fn lookup)
{
    return (elf_reloc_internal(lf, relocbase, data, type, 1, lookup));
}

int
elf_cpu_load_file(linker_file_t lf __unused)
{
    return (0);
}

int
elf_cpu_unload_file(linker_file_t lf __unused)
{
    return (0);
}

void
arc4rand(void *ptr, unsigned int len, int reseed)
{
    ff_arc4rand(ptr, len, reseed);
}

uint32_t
arc4random(void)
{
    return ff_arc4random();
}

void
random_harvest_queue(const void *entropy, u_int size,
    u_int bits, enum random_entropy_source origin)
{
    ;
}

u_int
read_random(void *buf, u_int count)
{
    arc4rand(buf, count, 0);
    return (count);
}

int
fubyte(volatile const void *base)
{
    return (*(volatile const uint8_t *)base);
}

void
timekeep_push_vdso(void)
{
    ;
}

uint32_t
cpu_fill_vdso_timehands(struct vdso_timehands *vdso_th, struct timecounter *tc)
{
    return (0);
}

