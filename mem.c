/* mem.c - memory budget probe: `chess memprobe`.
 *
 * Why this exists.  The linked image size says almost nothing about whether
 * another table will fit: DOS, the BIOS data area, the environment block and
 * the PSP are resident before us, and on the 512 KB atami image that is
 * 138.6 KB (see MEMORY.md, "Measured headroom").  A 45 KB table that looked
 * affordable from "512 KB minus the exe" turned out not to fit at all: 45 KB
 * against 23 KB of real free space.
 *
 * Two probes, one command:
 *
 *  16-bit target  - reads the live map through memprobe.asm: PSP, block top,
 *                   installed KB, the DOS free-block query, then walks the MCB
 *                   chain so the output locates the free space instead of only
 *                   totalling it.
 *  host build     - there is no DOS map to read, so it predicts the *artifact's*
 *                   free space from `chess.map`'s "Memory size" and gates it:
 *                   warn below 10 KB free, fail below 4 KB.  This is what
 *                   `make test` runs, and build.ps1 applies the same arithmetic
 *                   right after linking.
 *
 * The constants below are dated measurements, not guesses: the block DOS hands
 * the program above its PSP, the exe header size, and what DOS holds against
 * that block besides the image.  Re-measure with `chess memprobe` in the VM
 * after any change to DOS, the boot image or a large static allocation.
 */

#include "engine.h"

/* Measured 2026-11-10 in the 86Box atami image (512 KB, 80286): 382,112 bytes
   above the PSP, with 141,904 bytes already resident below us.  The 640 KB xt
   image has the same 141,904 below and 172,288 above, so atami binds. */
#define MEM_ATAMI_AVAIL 382112L
#define MEM_EXE_HDR       1840L   /* DOS exe header: cparhdr = 115 paragraphs */

/* Not image, not free: the PSP, the environment block and the runtime heap that
   the probe's MCB walk showed inside the program's own allocation.  Derived
   from the same measurement (382,112 - 1,840 - 341,712 image - 23,520 free), so
   the prediction tracks the VM's free block to within paragraph rounding, ~64 B. */
#define MEM_PSP_OVERHEAD 15040L

#define MEM_AVAIL (MEM_ATAMI_AVAIL - MEM_EXE_HDR - MEM_PSP_OVERHEAD)

/* Gate on free bytes above the image: 10 KB left is a warning, 4 KB a failure.
   The linking step and the runtime keep some slack, and the free block is the
   room a larger image actually grows into (MEMORY.md, "Measured headroom"). */
#define MEM_WARN 10240L
#define MEM_FAIL  4096L

#if defined(__WATCOMC__) && !defined(__386__)

#include <stdio.h>
#include <i86.h>

void memprobe(u16 __far *out);      /* memprobe.asm, 7 words */

#define MCB_MAX 20

static u16 mcb_word(u16 seg, u16 off) {
    return *(const u16 __far *)MK_FP(seg, off);
}

int mem_probe(void) {
    u16 mp[7];
    u16 seg, i, owner, size, free_par = 0, big_par = 0, ours_par = 0;
    u16 nfree = 0, psp, below_kb, block_kb;
    u32 big_free;

    memprobe(mp);
    psp = mp[1];
    /* Shift, not /64U: Watcom -0 miscompiles the 16-bit unsigned divide here
       (it printed the raw paragraph counts), and a shift cannot. */
    below_kb = (u16)((u32)psp >> 6);                      /* psp*16/1024 */
    block_kb = (mp[2] > psp) ? (u16)(((u32)mp[2] - psp) >> 6) : 0;

    printf("memprobe dos=%u.%02u installed=%uKB\n",
           (unsigned)(mp[0] >> 8), (unsigned)(mp[0] & 0xFF), (unsigned)mp[3]);
    printf("memprobe psp=%04x below=%uKB block=%uKB top=%04x\n",
           (unsigned)psp, (unsigned)below_kb, (unsigned)block_kb,
           (unsigned)mp[2]);

    /* Walk from the first MCB: owner 0 is free, 8 is DOS, our PSP is us. */
    seg = mp[6];
    for (i = 0; i < MCB_MAX; i++) {
        const u8 __far *h = (const u8 __far *)MK_FP(seg, 0);
        u8 type = h[0];
        owner = mcb_word(seg, 1);
        size  = mcb_word(seg, 3);
        if (size == 0) break;                   /* corrupt chain: stop */
        if (owner == 0) { free_par += size; ++nfree; if (size > big_par) big_par = size; }
        if (owner == psp) ours_par += size;
        printf("memprobe  %04x %s %5uKB owner=%04x%s\n",
               (unsigned)seg, (type == 0x5A) ? "end" : "blk",
               (unsigned)((u32)size / 64UL), (unsigned)owner,
               (owner == 0) ? " free" : (owner == psp) ? " us" : "");
        if (type == 0x5A) break;
        seg = (u16)(seg + size + 1);
    }

    printf("memprobe free=%uKB in %u block(s), largest=%uKB, our block=%uKB\n",
           (unsigned)((u32)free_par / 64UL), (unsigned)nfree,
           (unsigned)((u32)big_par / 64UL), (unsigned)((u32)ours_par / 64UL));
    big_free = (u32)big_par * 16UL;                 /* growth room for the image */
    printf("memprobe gate warn=%luB fail=%luB: image can grow %luB more %s\n",
           (unsigned long)MEM_WARN, (unsigned long)MEM_FAIL,
           (unsigned long)big_free,
           (big_free < (u32)MEM_FAIL) ? "FAIL" :
           (big_free < (u32)MEM_WARN) ? "WARN" : "ok");
    printf("memprobe avail=%luB for the image (map side: chess.map + build)\n",
           (unsigned long)MEM_AVAIL);
    return 0;
}

#else /* host build: check the DOS artifact instead of a live machine */

#include <stdio.h>
#include <string.h>

int mem_probe(void) {
    FILE *f = fopen("chess.map", "r");
    char line[256];
    long image = -1, headroom;

    if (!f) {
        printf("memprobe: no chess.map (the 16-bit build writes it), so the "
               "image side of the budget is not checked\n");
        return 0;
    }
    while (fgets(line, (int)sizeof line, f))
        if (strstr(line, "Memory size:")) {
            /* "Memory size:  000533a0 (340896.)" */
            if (sscanf(line, "Memory size: %*s (%ld", &image) != 1) image = -1;
            break;
        }
    fclose(f);
    if (image < 0) {
        printf("memprobe: chess.map has no parsable Memory size; not checked\n");
        return 0;
    }
    headroom = MEM_AVAIL - image;
    printf("memprobe image=%ldB avail=%ldB free=%ldB "
           "(warn<%ldB fail<%ldB) %s\n",
           image, (long)MEM_AVAIL, headroom,
           (long)MEM_WARN, (long)MEM_FAIL,
           (headroom < MEM_FAIL) ? "FAIL" :
           (headroom < MEM_WARN) ? "WARN" : "ok");
    if (headroom < MEM_FAIL)
        printf("memprobe: the 512 KB atami image has only %ldB of free space "
               "left, under the %ldB floor; re-measure with `chess memprobe` "
               "in the VM (MEMORY.md)\n", headroom, (long)MEM_FAIL);
    else if (headroom < MEM_WARN)
        printf("memprobe: warning - only %ldB free on the 512 KB atami image "
               "(under %ldB); the next table of this size will not fit\n",
               headroom, (long)MEM_WARN);
    return (headroom < MEM_FAIL) ? 1 : 0;
}

#endif
