; gen_opt.asm - hand-written capture generator (gen_caps) for the 16-bit target
;
; Why this exists.  The C body spends only 10.5% of its time on the 64-square
; sweep (1,735 of 16,531 cycles/call, measured with `sbench`); the other 89.5%
; is per-piece work, and almost all of that is slider rays.  A single ray step
; in the compiled code costs about 100 cycles:
;
;       test bl,88h        ; to & 0x88
;       jne  nextdir
;       mov  di,bx         ; square -> word address, three instructions
;       shl  di,1
;       add  di,si         ; ... rebuilding the board base every probe
;       mov  cx,[di]
;       test cx,cx
;       je   step          ; TAKEN on every empty square
;       add  bx,-2[bp]     ; to += d, re-loaded from the stack
;       jmp  scan          ; second taken jump
;
; Both `to` and the board address are recomputed from a stack frame on every
; square, the direction table is walked with a counted loop, and every move is
; encoded with the shift/mask/or sequence (sq2c twice plus <<6, about twelve
; instructions per move).
;
; This version keeps a byte offset instead of a square:
;
;   si = p->off                      (loaded once; DS = p->seg)
;   bx = 2*from                      (byte offset inside board[])
;   bp = 2*to                        (advances by 2*d along a ray)
;   di = move list cursor            (ES = list segment)
;   dx = 2*p->ep                     (call-wide, so the EP test is one CMP)
;
; With that, a probe is `mov ax,ds:[bp+si]` and the 0x88 test is a single
; `test bp,110h` on the doubled square (doubling maps bit 3 -> 4 and bit 7 ->
; 8, and the carry out of bit 15 can never be in 0x88).  A ray step is
;
;       test bp,110h
;       jne  nextdir
;       mov  ax,ds:[bp+si]
;       test ax,ax
;       jne  occ
;       add  bp,d2
;       jmp  scan
;
; Move encoding is two table reads: _mf[2*from] holds sq2c(from) and _mt[2*to]
; holds sq2c(to)<<6, both indexed by the doubled square, so the move is just
; `or ax,ss:_mt[bp]`.
;
; Branch polarity follows the is_attacked asm: inside a slider ray the miss
; path falls through and only the hit branches, and the emit is the last thing
; in a direction block, so a captured piece costs no extra jump.  Knight and
; king probes cannot share a hit stub (each probe has to resume at its own
; successor), so they test-and-skip instead: `test ...,8 / je next` for white
; and `dec/cmp 5/ja next` for black, then the emit inline.
;
; Sweep and dispatch (2026-11-04, second pass).  The 64-square sweep was half
; the routine after the first port: the C measurement above (10.5% sweep) no
; longer described the asm, because the per-piece work had become cheap.  The
; sweep now runs eight probes per rank as straight-line code; an occupied
; square resumes at the next probe through gc_w_resume/gc_b_resume indexed by
; the low five bits of bx (2*from), so there is no per-square file counter or
; wrap test.  The empty path is load/test/add only, and after file 7 the rank
; block adds the 16 off-board byte offsets and falls straight into the next
; rank.  Piece dispatch is one indirect jump through a
; 16-entry code table instead of a compare ladder, and moves are written with
; STOSW (DF is clear throughout the engine; no STD anywhere).  The resume and
; dispatch tables hold code offsets in this segment and are jumped to with a CS
; override.
;
; Measured on the Nimbus 80186: sbench gen_caps 7,189 -> 6,167 cycles/call
; (-14.2%); bench 4 2,059,959,947 -> 2,045,263,493 cycles (-0.71%), tree
; identical (47,627 nodes); `gencaps` 6,797 positions, 0 mismatches, order
; included.  86Box bench-4 guest time, separate boots: -0.40% (80286), -0.31%
; (8086), -0.39% (8088).  See artifacts/gencaps-sweep-2026-11-04/REPORT.md.
; Byte loads in the sweep/rays and LEA for the bp=bx+d steps measured an exact
; tie on the 80186 and were left out; unrolling the ray loop does not pay
; (55% of slider directions end within one square).
;
; 8086 notes.  Only BX/BP/SI/DI can appear in an addressing mode, which is why
; `from` is kept doubled in BX rather than in DX.  [bx+si] needs no segment
; prefix (DS is the default); [bp+si] does, because BP defaults to SS.
;
; Calling convention (Open Watcom 16-bit large model, __watcall):
;   ax:dx = Pos *p, bx:cx = u16 *list, result i16 in ax.
; Preserves si, di, bp; clobbers ax, bx, cx, dx, es and leaves ds = p->seg,
; exactly the footprint of the compiled C body.
;
; Pos layout used here: board[128] at 0, side 256, castle 258, ep 260.
; Piece codes: WP 1 WN 2 WB 3 WR 4 WQ 5 WK 6, BP 9 BN 10 BB 11 BR 12 BQ 13 BK 14.

        .8086
        NAME    gen_opt

_TEXT   SEGMENT BYTE PUBLIC USE16 'CODE'
        ASSUME  CS:_TEXT

        EXTRN   _mf:WORD                ; _mf[sq] = sq2c(sq)
        EXTRN   _mt:WORD                ; _mt[sq] = sq2c(sq) << 6

        PUBLIC  gen_caps_asm_

; ---------------------------------------------------------------------
; Move emission.  bx = 2*from, bp = 2*to, di = list cursor, es = list seg.
; The four promotion moves carry flags 1..4 (knight, bishop, rook, queen);
; the base word has bits 12..15 clear, so four ADDs walk the flag field.
; STOSW writes ES:[DI] and advances DI, replacing the store + ADD DI,2 pair;
; DF is clear everywhere in this engine (no STD in any C or asm source).
; ---------------------------------------------------------------------
MKEMIT  MACRO
        mov     ax, ss:_mf[bx]
        or      ax, ss:_mt[bp]
        stosw
        ENDM

MKEMITF MACRO   fl
        mov     ax, ss:_mf[bx]
        or      ax, ss:_mt[bp]
        or      ax, fl
        stosw
        ENDM

MKEMIT4 MACRO
        mov     ax, ss:_mf[bx]
        or      ax, ss:_mt[bp]
        add     ax, 1000h
        stosw
        add     ax, 1000h
        stosw
        add     ax, 1000h
        stosw
        add     ax, 1000h
        stosw
        ENDM

; ---------------------------------------------------------------------
; Slider ray.  entry is this direction's label, exit the next direction's
; (or the piece epilogue).  A hit falls through into exit because the emit
; is the last thing in the block: same-colour blockers jump to exit, an
; enemy piece is captured and then the next direction starts.
; d2 = 2*d, the byte delta.
; ---------------------------------------------------------------------
RAYW    MACRO   d2, entry, exit
        LOCAL   scan, occ
entry:
        mov     bp, bx
        add     bp, d2
scan:
        test    bp, 110h
        jne     short exit
        mov     ax, ds:[bp+si]
        test    ax, ax
        jne     short occ
        add     bp, d2
        jmp     short scan
occ:
        test    al, 8                   ; white: bit 3 set = black piece
        je      short exit
        MKEMIT
        ENDM

RAYB    MACRO   d2, entry, exit
        LOCAL   scan, occ
entry:
        mov     bp, bx
        add     bp, d2
scan:
        test    bp, 110h
        jne     short exit
        mov     ax, ds:[bp+si]
        test    ax, ax
        jne     short occ
        add     bp, d2
        jmp     short scan
occ:
        test    al, 8                   ; black: bit 3 clear = white piece
        jne     short exit
        MKEMIT
        ENDM

; =====================================================================
; i16 gen_caps_asm(Pos *p, u16 *list)
; =====================================================================
gen_caps_asm_ PROC FAR
        push    si
        push    di
        push    bp
        push    bx                      ; list base, for the count at exit
        mov     si, ax                  ; si = p->off
        mov     ds, dx                  ; ds = p->seg
        mov     es, cx                  ; es = list segment
        mov     di, bx                  ; di = list cursor
        mov     dx, [si+260]            ; p->ep
        add     dx, dx                  ; 2*ep (negative when there is none)
        cmp     word ptr [si+256], 0    ; side
        jne     gc_b_top

; ---------------------------------------------------------------- white
; Sweep the 64 on-board squares in ascending order.  bx is the byte offset
; 2*from and is never advanced speculatively: each probe loads [bx+si], and an
; empty square adds 2 before falling into the next probe.  After file 7 the
; rank block adds 16 more, so the eight probes form one straight-line rank.
; An occupied square jumps to gc_w_occ and then resumes through gc_w_next,
; whose table entry is the probe after that file (file 7 resumes at gc_w_rank).
; This removes the per-square file counter/wrap test that the first port had.
        xor     bx, bx
gc_w_probe0:
        mov     ax, [bx+si]             ; board[from], ds already = p->seg
        test    ax, ax
        jne     gc_w_occ
        add     bx, 2
gc_w_probe1:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_w_occ
        add     bx, 2
gc_w_probe2:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_w_occ
        add     bx, 2
gc_w_probe3:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_w_occ
        add     bx, 2
gc_w_probe4:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_w_occ
        add     bx, 2
gc_w_probe5:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_w_occ
        add     bx, 2
gc_w_probe6:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_w_occ
        add     bx, 2
gc_w_probe7:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_w_occ
        add     bx, 2
gc_w_rank:
        add     bx, 16                  ; skip the rank's off-board half
        cmp     bx, 0F0h                ; past h8: the sweep is done
        jae     gc_done
        jmp     short gc_w_probe0

; An occupied square resumes at the next probe through this table, so the
; per-square file counter disappears.  bx still holds 2*from; the low five
; bits are 2*file, which is exactly the table byte offset.
gc_w_next:
        mov     bp, bx
        and     bp, 1Eh
        add     bx, 2
        jmp     word ptr cs:gc_w_resume[bp]
gc_w_resume:
        dw      gc_w_probe1, gc_w_probe2, gc_w_probe3, gc_w_probe4
        dw      gc_w_probe5, gc_w_probe6, gc_w_probe7, gc_w_rank

gc_w_occ:
        test    al, 8
        jne     short gc_w_next         ; black piece
        and     ax, 0Fh
        shl     ax, 1
        mov     bp, ax
        jmp     word ptr cs:gc_tab[bp]
gc_w_t_kn:  jmp     gc_w_knight
gc_w_t_king: jmp    gc_w_king

gc_tab:
        dw      gc_w_next, gc_w_pawn, gc_w_knight, gc_w_bishop
        dw      gc_w_rook, gc_w_queen, gc_w_king, gc_w_next
        dw      gc_w_next, gc_b_pawn, gc_b_knight, gc_b_bishop
        dw      gc_b_rook, gc_b_queen, gc_b_king, gc_w_next

; ---- white pawn ------------------------------------------------------
; r == 6  <=>  (2*from & 0xE0) == 0xC0.  dx = 2*ep.
gc_w_pawn:
        mov     ax, bx
        and     ax, 0E0h
        cmp     ax, 0C0h
        je      short gc_w_prom

        mov     bp, bx
        add     bp, 30                  ; to = from+15
        test    bp, 110h
        jne     short gc_w_p2
        cmp     bp, dx
        je      short gc_w_ep1
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_p2
        MKEMIT
gc_w_p2:
        mov     bp, bx
        add     bp, 34                  ; to = from+17
        test    bp, 110h
        jne     short gc_w_pend
        cmp     bp, dx
        je      short gc_w_ep2
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_pend
        MKEMIT
gc_w_pend:
        jmp     gc_w_next
gc_w_ep1:
        MKEMITF 5000h
        jmp     short gc_w_p2
gc_w_ep2:
        MKEMITF 5000h
        jmp     short gc_w_pend

; promoting pawn: the push and every capture emit all four pieces
gc_w_prom:
        cmp     word ptr [bx+si+32], 0  ; board[from+16] empty?
        jne     short gc_w_pr2
        mov     bp, bx
        add     bp, 32
        MKEMIT4
gc_w_pr2:
        mov     bp, bx
        add     bp, 30
        test    bp, 110h
        jne     short gc_w_pr3
        cmp     bp, dx
        je      short gc_w_prep1
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_pr3
        MKEMIT4
gc_w_pr3:
        mov     bp, bx
        add     bp, 34
        test    bp, 110h
        jne     short gc_w_pre
        cmp     bp, dx
        je      short gc_w_prep2
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_pre
        MKEMIT4
gc_w_pre:
        jmp     gc_w_next
gc_w_prep1:
        MKEMITF 5000h
        jmp     short gc_w_pr3
gc_w_prep2:
        MKEMITF 5000h
        jmp     short gc_w_pre

; ---- white knight ----------------------------------------------------
; kn[] doubled: -66 -62 -36 -28 28 36 62 66.  `test ... ,8` is the whole
; capture test for white: bit 3 is set only on a black piece, and an empty
; square is zero.  Capture emits inline, so only the hit pays a branch.
gc_w_knight:
        mov     bp, bx
        sub     bp, 66
        test    bp, 110h
        jne     short gc_w_n1
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_n1
        MKEMIT
gc_w_n1:
        mov     bp, bx
        sub     bp, 62
        test    bp, 110h
        jne     short gc_w_n2
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_n2
        MKEMIT
gc_w_n2:
        mov     bp, bx
        sub     bp, 36
        test    bp, 110h
        jne     short gc_w_n3
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_n3
        MKEMIT
gc_w_n3:
        mov     bp, bx
        sub     bp, 28
        test    bp, 110h
        jne     short gc_w_n4
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_n4
        MKEMIT
gc_w_n4:
        mov     bp, bx
        add     bp, 28
        test    bp, 110h
        jne     short gc_w_n5
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_n5
        MKEMIT
gc_w_n5:
        mov     bp, bx
        add     bp, 36
        test    bp, 110h
        jne     short gc_w_n6
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_n6
        MKEMIT
gc_w_n6:
        mov     bp, bx
        add     bp, 62
        test    bp, 110h
        jne     short gc_w_n7
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_n7
        MKEMIT
gc_w_n7:
        mov     bp, bx
        add     bp, 66
        test    bp, 110h
        jne     short gc_w_nend
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_nend
        MKEMIT
gc_w_nend:
        jmp     gc_w_next

; ---- white king ------------------------------------------------------
; ki[] doubled: -34 -32 -30 -2 2 30 32 34
gc_w_king:
        mov     bp, bx
        sub     bp, 34
        test    bp, 110h
        jne     short gc_w_g1
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_g1
        MKEMIT
gc_w_g1:
        mov     bp, bx
        sub     bp, 32
        test    bp, 110h
        jne     short gc_w_g2
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_g2
        MKEMIT
gc_w_g2:
        mov     bp, bx
        sub     bp, 30
        test    bp, 110h
        jne     short gc_w_g3
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_g3
        MKEMIT
gc_w_g3:
        mov     bp, bx
        sub     bp, 2
        test    bp, 110h
        jne     short gc_w_g4
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_g4
        MKEMIT
gc_w_g4:
        mov     bp, bx
        add     bp, 2
        test    bp, 110h
        jne     short gc_w_g5
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_g5
        MKEMIT
gc_w_g5:
        mov     bp, bx
        add     bp, 30
        test    bp, 110h
        jne     short gc_w_g6
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_g6
        MKEMIT
gc_w_g6:
        mov     bp, bx
        add     bp, 32
        test    bp, 110h
        jne     short gc_w_g7
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_g7
        MKEMIT
gc_w_g7:
        mov     bp, bx
        add     bp, 34
        test    bp, 110h
        jne     short gc_w_gend
        test    byte ptr ds:[bp+si], 8
        je      short gc_w_gend
        MKEMIT
gc_w_gend:
        jmp     gc_w_next

; ---- white sliders ---------------------------------------------------
; Unreachable since gc_tab jumps straight to gc_w_rook/queen/bishop.  Kept
; deliberately: deleting the two old ladders shifted the following handlers by
; ~50 bytes and measured 0.078% slower over `bench 4` on the 80186
; (2,046,861,605 vs 2,045,263,493 cycles), so the space is acting as a free
; alignment pad.  Do not "clean it up" without re-measuring.
gc_w_slider:
        cmp     al, 5
        je      short gc_w_t_q
        cmp     al, 4
        je      short gc_w_t_r
        jmp     short gc_w_t_b
gc_w_t_q:   jmp     gc_w_queen
gc_w_t_r:   jmp     gc_w_rook
gc_w_t_b:   jmp     gc_w_bishop

gc_w_rook:
        RAYW  -32, gc_w_r1, gc_w_r2      ; -16 north
        RAYW    2, gc_w_r2, gc_w_r3      ;  +1 east
        RAYW   32, gc_w_r3, gc_w_r4      ; +16 south
        RAYW   -2, gc_w_r4, gc_w_rend    ;  -1 west
gc_w_rend:
        jmp     gc_w_next

gc_w_bishop:
        RAYW  -34, gc_w_b1, gc_w_b2      ; -17 north-east
        RAYW  -30, gc_w_b2, gc_w_b3      ; -15 north-west
        RAYW   30, gc_w_b3, gc_w_b4      ; +15 south-west
        RAYW   34, gc_w_b4, gc_w_bend    ; +17 south-east
gc_w_bend:
        jmp     gc_w_next

gc_w_queen:
        RAYW  -34, gc_w_q1, gc_w_q2
        RAYW  -32, gc_w_q2, gc_w_q3
        RAYW  -30, gc_w_q3, gc_w_q4
        RAYW   -2, gc_w_q4, gc_w_q5
        RAYW    2, gc_w_q5, gc_w_q6
        RAYW   30, gc_w_q6, gc_w_q7
        RAYW   32, gc_w_q7, gc_w_q8
        RAYW   34, gc_w_q8, gc_w_qend
gc_w_qend:
        jmp     gc_w_next

; ---------------------------------------------------------------- black
gc_b_top:
        xor     bx, bx
gc_b_probe0:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_b_occ
        add     bx, 2
gc_b_probe1:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_b_occ
        add     bx, 2
gc_b_probe2:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_b_occ
        add     bx, 2
gc_b_probe3:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_b_occ
        add     bx, 2
gc_b_probe4:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_b_occ
        add     bx, 2
gc_b_probe5:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_b_occ
        add     bx, 2
gc_b_probe6:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_b_occ
        add     bx, 2
gc_b_probe7:
        mov     ax, [bx+si]
        test    ax, ax
        jne     gc_b_occ
        add     bx, 2
gc_b_rank:
        add     bx, 16                  ; skip the rank's off-board half
        cmp     bx, 0F0h                ; past h8: the sweep is done
        jae     gc_done
        jmp     short gc_b_probe0

gc_b_next:
        mov     bp, bx
        and     bp, 1Eh
        add     bx, 2
        jmp     word ptr cs:gc_b_resume[bp]
gc_b_resume:
        dw      gc_b_probe1, gc_b_probe2, gc_b_probe3, gc_b_probe4
        dw      gc_b_probe5, gc_b_probe6, gc_b_probe7, gc_b_rank

gc_b_occ:
        test    al, 8
        je      short gc_b_next         ; white piece
        and     ax, 0Fh
        shl     ax, 1
        mov     bp, ax
        jmp     word ptr cs:gc_tab[bp]
gc_b_t_kn:  jmp     gc_b_knight
gc_b_t_king: jmp    gc_b_king

; ---- black pawn ------------------------------------------------------
; r == 1  <=>  (2*from & 0xE0) == 0x20.  Attacks are nonzero AND bit 3 clear.
gc_b_pawn:
        mov     ax, bx
        and     ax, 0E0h
        cmp     ax, 20h
        je      short gc_b_prom

        mov     bp, bx
        sub     bp, 34                  ; to = from-17
        test    bp, 110h
        jne     short gc_b_p2
        cmp     bp, dx
        je      short gc_b_ep1
        mov     al, ds:[bp+si]
        test    al, al
        je      short gc_b_p2
        test    al, 8
        jne     short gc_b_p2
        MKEMIT
gc_b_p2:
        mov     bp, bx
        sub     bp, 30                  ; to = from-15
        test    bp, 110h
        jne     short gc_b_pend
        cmp     bp, dx
        je      short gc_b_ep2
        mov     al, ds:[bp+si]
        test    al, al
        je      short gc_b_pend
        test    al, 8
        jne     short gc_b_pend
        MKEMIT
gc_b_pend:
        jmp     gc_b_next
gc_b_ep1:
        MKEMITF 5000h
        jmp     short gc_b_p2
gc_b_ep2:
        MKEMITF 5000h
        jmp     short gc_b_pend

gc_b_prom:
        cmp     word ptr [bx+si-32], 0  ; board[from-16] empty?
        jne     short gc_b_pr2
        mov     bp, bx
        sub     bp, 32
        MKEMIT4
gc_b_pr2:
        mov     bp, bx
        sub     bp, 34
        test    bp, 110h
        jne     short gc_b_pr3
        cmp     bp, dx
        je      short gc_b_prep1
        mov     al, ds:[bp+si]
        test    al, al
        je      short gc_b_pr3
        test    al, 8
        jne     short gc_b_pr3
        MKEMIT4
gc_b_pr3:
        mov     bp, bx
        sub     bp, 30
        test    bp, 110h
        jne     short gc_b_pre
        cmp     bp, dx
        je      short gc_b_prep2
        mov     al, ds:[bp+si]
        test    al, al
        je      short gc_b_pre
        test    al, 8
        jne     short gc_b_pre
        MKEMIT4
gc_b_pre:
        jmp     gc_b_next
gc_b_prep1:
        MKEMITF 5000h
        jmp     short gc_b_pr3
gc_b_prep2:
        MKEMITF 5000h
        jmp     short gc_b_pre

; ---- black knight ----------------------------------------------------
; `dec/cmp 5/ja` is the black capture test: only 1..6 (white pieces) survive
; it, so an empty square and a black piece both fall through to the skip.
gc_b_knight:
        mov     bp, bx
        sub     bp, 66
        test    bp, 110h
        jne     short gc_b_n1
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_n1
        MKEMIT
gc_b_n1:
        mov     bp, bx
        sub     bp, 62
        test    bp, 110h
        jne     short gc_b_n2
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_n2
        MKEMIT
gc_b_n2:
        mov     bp, bx
        sub     bp, 36
        test    bp, 110h
        jne     short gc_b_n3
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_n3
        MKEMIT
gc_b_n3:
        mov     bp, bx
        sub     bp, 28
        test    bp, 110h
        jne     short gc_b_n4
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_n4
        MKEMIT
gc_b_n4:
        mov     bp, bx
        add     bp, 28
        test    bp, 110h
        jne     short gc_b_n5
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_n5
        MKEMIT
gc_b_n5:
        mov     bp, bx
        add     bp, 36
        test    bp, 110h
        jne     short gc_b_n6
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_n6
        MKEMIT
gc_b_n6:
        mov     bp, bx
        add     bp, 62
        test    bp, 110h
        jne     short gc_b_n7
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_n7
        MKEMIT
gc_b_n7:
        mov     bp, bx
        add     bp, 66
        test    bp, 110h
        jne     short gc_b_nend
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_nend
        MKEMIT
gc_b_nend:
        jmp     gc_b_next

; ---- black king ------------------------------------------------------
; ki[] doubled: -34 -32 -30 -2 2 30 32 34
gc_b_king:
        mov     bp, bx
        sub     bp, 34
        test    bp, 110h
        jne     short gc_b_g1
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_g1
        MKEMIT
gc_b_g1:
        mov     bp, bx
        sub     bp, 32
        test    bp, 110h
        jne     short gc_b_g2
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_g2
        MKEMIT
gc_b_g2:
        mov     bp, bx
        sub     bp, 30
        test    bp, 110h
        jne     short gc_b_g3
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_g3
        MKEMIT
gc_b_g3:
        mov     bp, bx
        sub     bp, 2
        test    bp, 110h
        jne     short gc_b_g4
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_g4
        MKEMIT
gc_b_g4:
        mov     bp, bx
        add     bp, 2
        test    bp, 110h
        jne     short gc_b_g5
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_g5
        MKEMIT
gc_b_g5:
        mov     bp, bx
        add     bp, 30
        test    bp, 110h
        jne     short gc_b_g6
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_g6
        MKEMIT
gc_b_g6:
        mov     bp, bx
        add     bp, 32
        test    bp, 110h
        jne     short gc_b_g7
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_g7
        MKEMIT
gc_b_g7:
        mov     bp, bx
        add     bp, 34
        test    bp, 110h
        jne     short gc_b_gend
        mov     al, ds:[bp+si]
        dec     al
        cmp     al, 5
        ja      short gc_b_gend
        MKEMIT
gc_b_gend:
        jmp     gc_b_next

; ---- black sliders ---------------------------------------------------
; Unreachable; see the white slider note (the pair is kept for alignment).
gc_b_slider:
        cmp     al, 5
        je      short gc_b_t_q
        cmp     al, 4
        je      short gc_b_t_r
        jmp     short gc_b_t_b
gc_b_t_q:   jmp     gc_b_queen
gc_b_t_r:   jmp     gc_b_rook
gc_b_t_b:   jmp     gc_b_bishop

gc_b_rook:
        RAYB  -32, gc_b_r1, gc_b_r2
        RAYB    2, gc_b_r2, gc_b_r3
        RAYB   32, gc_b_r3, gc_b_r4
        RAYB   -2, gc_b_r4, gc_b_rend
gc_b_rend:
        jmp     gc_b_next

gc_b_bishop:
        RAYB  -34, gc_b_b1, gc_b_b2
        RAYB  -30, gc_b_b2, gc_b_b3
        RAYB   30, gc_b_b3, gc_b_b4
        RAYB   34, gc_b_b4, gc_b_bend
gc_b_bend:
        jmp     gc_b_next

gc_b_queen:
        RAYB  -34, gc_b_q1, gc_b_q2
        RAYB  -32, gc_b_q2, gc_b_q3
        RAYB  -30, gc_b_q3, gc_b_q4
        RAYB   -2, gc_b_q4, gc_b_q5
        RAYB    2, gc_b_q5, gc_b_q6
        RAYB   30, gc_b_q6, gc_b_q7
        RAYB   32, gc_b_q7, gc_b_q8
        RAYB   34, gc_b_q8, gc_b_qend
gc_b_qend:
        jmp     gc_b_next

; ---------------------------------------------------------------- result
gc_done:
        mov     ax, di
        pop     bx                      ; list base
        sub     ax, bx
        shr     ax, 1
        pop     bp
        pop     di
        pop     si
        ret
gen_caps_asm_ ENDP

_TEXT   ENDS
        END
