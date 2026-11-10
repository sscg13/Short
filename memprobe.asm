; memprobe.asm - read the live DOS memory map for `chess memprobe` (mem.c)
;
; Why this exists.  On the 16-bit target the image size says very little about
; whether a table will fit: DOS, the BIOS data area, the environment block and
; the PSP are resident before us, and on the 512 KB atami image that is 138.6 KB
; (MEMORY.md, "Measured headroom").  This routine reports the facts so a memory
; decision is measured, never inferred from `Memory size`.
;
; It fills a u16 array with, in order:
;
;   [0] DOS version from INT 21h AH=30h (AL major, AH minor)
;   [1] PSP segment (INT 21h AH=62h)
;   [2] first paragraph after this program's block (PSP:[0002])
;   [3] installed conventional memory in KB (0040:0013)
;   [4] largest free block in paragraphs (INT 21h AH=48h with BX=FFFF)
;   [5] that call's error code (0 on success)
;   [6] first MCB, i.e. the environment block's header, so mem.c can walk the
;       chain from its start (PSP:[002C] - 1)
;
; Calling convention (OpenWatcom 16-bit large model, __watcall): ax = out
; offset, dx = out segment, result is the filled array.  Preserves si, di, bp,
; ds; clobbers ax, bx, cx, dx, es (all of which the caller must assume).
;
; INT 21h is only trusted for the registers it documents (AX/BX), so the out
; pointer is reloaded from the stack frame before every store.

        .8086
        NAME    memprobe

_TEXT   SEGMENT BYTE PUBLIC USE16 'CODE'
        ASSUME  CS:_TEXT

        PUBLIC  memprobe_

OUTOFF  EQU     -2
OUTSEG  EQU     -4
FRAME   EQU     4

RELOAD  MACRO
        mov     di, [bp+OUTOFF]
        mov     es, [bp+OUTSEG]
        ENDM

memprobe_   PROC FAR
        push    bp
        push    si
        push    di
        push    ds
        push    es
        mov     bp, sp
        sub     sp, FRAME
        mov     [bp+OUTOFF], ax
        mov     [bp+OUTSEG], dx

; --- out[0]: DOS version -------------------------------------------------
        mov     ax, 3000h
        int     21h
        RELOAD
        mov     es:[di+0], ax

; --- out[1] and [2]: PSP and the top of our block ------------------------
        mov     ax, 6200h
        int     21h
        RELOAD
        mov     es:[di+2], bx           ; PSP segment
        mov     ax, bx
        mov     es, ax
        mov     ax, es:[0002h]          ; top of the block DOS gave us
        RELOAD
        mov     es:[di+4], ax

; --- out[6]: first MCB, from the PSP's environment pointer ---------------
        mov     ax, bx
        mov     es, ax
        mov     ax, es:[002Ch]          ; environment segment = first MCB + 1
        dec     ax
        RELOAD
        mov     es:[di+12], ax

; --- out[3]: installed KB from the BIOS data area -----------------------
        push    es
        mov     ax, 0040h
        mov     es, ax
        mov     ax, es:[0013h]
        pop     es
        RELOAD
        mov     es:[di+6], ax

; --- out[4..5]: largest free block --------------------------------------
        mov     ax, 4800h
        mov     bx, 0FFFFh
        int     21h
        RELOAD
        mov     es:[di+8], bx
        mov     es:[di+10], ax

        mov     sp, bp
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     bp
        retf
memprobe_   ENDP

_TEXT   ENDS
        END
