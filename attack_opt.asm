; attack_opt.asm - hand-written is_attacked for the 16-bit target
;
; Why this exists.  The C body is already at its instruction-count floor: the
; probe list is unrolled with immediate offsets and the 0x88 test is a single
; TEST.  What it still pays is the large-model far-pointer tax, on EVERY probe:
;
;       mov  ax,[bp-2]     ; sq re-materialised from the stack
;       sub  ax,021H
;       test al,88H
;       jne  next
;       shl  ax,1          ; square -> word index
;       mov  bx,si         ; rebuild the board base
;       mov  ds,di         ; reload the segment from the far pointer
;       add  bx,ax
;       cmp  dx,[bx]
;       je   hit
;
; Ten instructions and 22 bytes, of which four carry no information and one is
; a second memory read of a value that never changed.  None of it is expressible
; in C: the compiler cannot prove the far pointer's segment is stable, cannot
; hoist the base through a struct member, and has no register left after the
; frame.  Hand asm hoists all of it once:
;
;   * DS is loaded once at entry (the C body also leaves DS = p->seg),
;   * di keeps p->off, bx = p->off + 2*to and advances by 2*delta, so a probe
;     is a single word compare at [bx + disp8],
;   * sq stays in cx, so the 0x88 test never touches memory,
;   * near tables are addressed with ss: overrides, exactly as Watcom does,
;     so they never disturb DS.
;
; Branch polarity is the other half of the job.  On the 8086 family a taken
; conditional jump costs 15-16 cycles against 4 not-taken, so the MISS path -
; which is almost every probe of almost every call - must fall through, and
; only the rare hit may branch.  8086 has no near conditional jump, so the hit
; target has to be within +-127 bytes; the probes therefore share two hit stubs
; per colour (one per half-chain, hopped over once per call) instead of putting
; a far JMP after each compare.  The ray scan gets its stub for free: it sits
; in the gap between the loop's backward JMP and the piece test, which no
; fall-through path can reach.
;
; Implementation covers every piece type: pawns, knights, king (a single
; king_line adjacency bit instead of eight probes), and the eight slider rays.
; The ray loop runs four squares per backward branch.  It was brought up in
; stages and validated by `atkdiff` (asm against the C reference on all 128
; squares for both sides at every node walked) and timed at each step.
;
; Probe order (2026-11-04).  A pawn attacks sq from exactly the first square of
; the two diagonal rays that point back at its own side, so those two rays run
; FIRST and their first step is the pawn probe (RAYPAWN): one load decides pawn
; (hit), bishop or queen (hit), or blocked (ray dead), where the old chain
; probed the same word twice.  Order after them is knights, king, the four
; straight rays, then the two remaining diagonals.  This is not only about the
; saved load - those two diagonals are where a side's own sliders usually stand
; when the question is about its king, and scanning them last made every
; diagonal hit pay for four straight rays first.  Measured on the Nimbus 80186:
; is_attacked 1,081 -> 1,041 cycles/call, whole depth-4 search -0.23%, bench
; tree identical, `atkdiff` 850,816 checks 0 mismatches; 86Box bench-4 guest
; time -0.33% (80286), -0.21% (8086), -0.12% (8088).  See
; artifacts/atkfold-2026-11-04/REPORT.md.
;
; The order is the only thing that changed; the set of probes did not, so
; `atkdiff` stays the whole correctness argument.  NOTE for any future
; reordering: the rays walk BX, and the pawn/knight probes address [bx+disp8],
; so bx = p->off + 2*sq has to be rebuilt after the last ray that runs before
; them.
;
; 8086 notes.  Only BX/BP/SI/DI can appear in an addressing mode, so p->off
; must live in DI (not DX).
;
; Calling convention (OpenWatcom 16-bit large model, __watcall):
;   ax:dx = Pos *p, bx = sq, cx = by, result in ax (0/1).
; Preserves si, di, bp; clobbers ax, bx, cx, dx and sets ds = p->seg, which is
; exactly the register footprint the compiled C body already has.
;
; Pos layout used here: board[128] at 0, ks[2] at 262 (see engine.h).
; Piece codes: WP 1 WN 2 WB 3 WR 4 WQ 5 WK 6, BP 9 BN 10 BB 11 BR 12 BQ 13 BK 14.

        .8086
        NAME    attack_opt

_TEXT   SEGMENT BYTE PUBLIC USE16 'CODE'
        ASSUME  CS:_TEXT

        EXTRN   _king_line:BYTE

        PUBLIC  is_attacked_asm_

; ---------------------------------------------------------------------
; Ray scan for one direction, four squares per backward branch.
;   d  = square delta, d2 = 2*d (board bytes)
;   p0,p1 = the two slider codes that attack along this line, for one color
; On entry ax = to, bx = &board[to], cx = sq, di = p->off, dx = scratch.
; The expansion resets ax/bx from cx, so directions are independent.
;
; Layout note: `h` (the hit stub) sits between the loop's `jmp scan` and the
; piece test `occ`.  The loop reaches `occ` with a short JNE that skips `h`, so
; `h` is only ever entered by the two backward JE below - the non-matching
; piece falls straight through to the next direction with no taken branch.
; ---------------------------------------------------------------------
RAY     MACRO   d, d2, p0, p1
        LOCAL   scan, occ, h, nxt
        mov     ax, cx
        mov     bx, cx
        shl     bx, 1
        add     bx, di                  ; &board[sq]
        add     ax, d
        add     bx, d2
scan:
        REPT    4
        test    al, 88h
        jne     short nxt
        mov     dx, [bx]
        test    dx, dx
        jne     short occ
        add     ax, d
        add     bx, d2
        ENDM
        jmp     short scan
h:
        jmp     atk_hit
occ:
        cmp     dx, p0
        je      short h
        cmp     dx, p1
        je      short h
nxt:
ENDM

; ---------------------------------------------------------------------
; Same as RAY, but the FIRST square is also the pawn-attack square of this
; direction, so the first step doubles as the pawn probe: one load decides
; pawn (hit), bishop/queen (hit) and "blocked" instead of a separate probe
; followed by a re-probe of the same word.
;   d  = square delta, d2 = 2*d, p0,p1 = the two slider codes
;   pw = the pawn code of the colour that owns those rays
; ---------------------------------------------------------------------
RAYPAWN MACRO   d, d2, p0, p1, pw
        LOCAL   scan, occ, h, nxt, first
        mov     ax, cx
        mov     bx, cx
        shl     bx, 1
        add     bx, di                  ; &board[sq]
        add     ax, d
        add     bx, d2
        test    al, 88h
        jne     short nxt               ; first step off board: no ray at all
        mov     dx, [bx]
        test    dx, dx
        jne     short first
        add     ax, d                   ; first square empty: scan from step 2
        add     bx, d2
scan:
        REPT    4
        test    al, 88h
        jne     short nxt
        mov     dx, [bx]
        test    dx, dx
        jne     short occ
        add     ax, d
        add     bx, d2
        ENDM
        jmp     short scan
h:
        jmp     atk_hit
first:
        cmp     dx, pw                  ; pawn attacks from here
        je      short h
occ:
        cmp     dx, p0
        je      short h
        cmp     dx, p1
        je      short h
nxt:
ENDM

; =====================================================================
; i16 is_attacked_asm(Pos *p, i16 sq, i16 by)
; =====================================================================
is_attacked_asm_ PROC FAR
        push    di
        mov     ds, dx                  ; ds = p->seg (left that way on return,
                                        ; same as the compiled C body)
        mov     di, ax                  ; di = p->off
        test    cx, cx
        jne     atk_black

; ---------------------------------------------------------------- white
        mov     cx, bx                  ; cx = sq (stays here for 0x88 tests)
        shl     bx, 1
        add     bx, ax                  ; bx = p->off + 2*sq

; The two squares a white pawn attacks sq from, sq-17 and sq-15, are the first
; squares of the NE and NW rays, so those two rays run first and their first
; step is the pawn probe (RAYPAWN).  A pawn there is an attacker, a bishop or
; queen there is an attacker, and anything else blocks the ray - exactly what
; the pawn probe plus the ray's own first step decided, with one load instead
; of two.  The pawn squares are also where white's sliders usually stand when
; the question is about the white king, so this order exits sooner as well.
        RAYPAWN -17, -34, 3, 5, 1       ; NE (+ WP)
        RAYPAWN -15, -30, 3, 5, 1       ; NW (+ WP)

; The ray scan walks bx; the probe chain below addresses [bx+disp8], so the
; base goes back before the knights (ax and dx are dead here too).
        mov     bx, cx
        shl     bx, 1
        add     bx, di

; knights: -33 -31 -18 (the rest follow the stub)
atk_w_n1:
        mov     ax, cx
        sub     ax, 33
        test    al, 88h
        jne     short atk_w_n2
        cmp     word ptr [bx-66], 2     ; WN
        je      short atk_w_hit
atk_w_n2:
        mov     ax, cx
        sub     ax, 31
        test    al, 88h
        jne     short atk_w_n3
        cmp     word ptr [bx-62], 2
        je      short atk_w_hit
atk_w_n3:
        mov     ax, cx
        sub     ax, 18
        test    al, 88h
        jne     short atk_w_n4
        cmp     word ptr [bx-36], 2
        je      short atk_w_hit

; Hit stub, hopped over once per call and reachable by a short JE from every
; probe of this colour (the chain is 176 bytes, the stub sits at ~80).
        jmp     short atk_w_go
atk_w_hit:
        jmp     atk_hit
atk_w_go:
atk_w_n4:
        mov     ax, cx
        sub     ax, 14
        test    al, 88h
        jne     short atk_w_n5
        cmp     word ptr [bx-28], 2
        je      short atk_w_hit
atk_w_n5:
        mov     ax, cx
        add     ax, 14
        test    al, 88h
        jne     short atk_w_n6
        cmp     word ptr [bx+28], 2
        je      short atk_w_hit
atk_w_n6:
        mov     ax, cx
        add     ax, 18
        test    al, 88h
        jne     short atk_w_n7
        cmp     word ptr [bx+36], 2
        je      short atk_w_hit
atk_w_n7:
        mov     ax, cx
        add     ax, 31
        test    al, 88h
        jne     short atk_w_n8
        cmp     word ptr [bx+62], 2
        je      short atk_w_hit
atk_w_n8:
        mov     ax, cx
        add     ax, 33
        test    al, 88h
        jne     short atk_w_king
        cmp     word ptr [bx+66], 2
        je      short atk_w_hit

; king: board[ks[0]] must still hold WK (diagnostic FENs can break the
; maintained invariant), then the adjacency bit replaces eight probes.
atk_w_king:
        mov     ax, [di+262]            ; to = p->ks[0]
        test    ax, ax                  ; unset king (diagnostic FEN): no attack
        js      short atk_w_rays
        mov     bx, ax
        shl     bx, 1
        add     bx, di                  ; &board[to]
        cmp     word ptr [bx], 6        ; WK
        jne     short atk_w_rays
        sub     ax, cx                  ; to - sq
        mov     bx, ax
        mov     bh, 0                   ; low byte only: king_line[(u8)(to-sq)]
        test    byte ptr ss:_king_line[bx], 2
        jne     short atk_w_hit         ; adjacency: hit (rare, backward)

atk_w_rays:
        RAY     -16, -32, 4, 5          ; north
        RAY       1,   2, 4, 5          ; east
        RAY      16,  32, 4, 5          ; south
        RAY      -1,  -2, 4, 5          ; west
        RAY      15,  30, 3, 5          ; SW (NE/NW already scanned above)
        RAY      17,  34, 3, 5          ; SE
; fall through: nothing attacks this square
        xor     ax, ax
        pop     di
        ret

; ---------------------------------------------------------------- black
atk_black:
        mov     cx, bx                  ; cx = sq
        shl     bx, 1
        add     bx, ax                  ; bx = p->off + 2*sq

; Black's pawn squares, sq+15 and sq+17, are the first squares of SW and SE.
        RAYPAWN  15,  30, 11, 13, 9     ; SW (+ BP)
        RAYPAWN  17,  34, 11, 13, 9     ; SE (+ BP)

        mov     bx, cx                  ; restore the base the probes use
        shl     bx, 1
        add     bx, di

atk_b_n1:
        mov     ax, cx
        sub     ax, 33
        test    al, 88h
        jne     short atk_b_n2
        cmp     word ptr [bx-66], 10    ; BN
        je      short atk_b_hit
atk_b_n2:
        mov     ax, cx
        sub     ax, 31
        test    al, 88h
        jne     short atk_b_n3
        cmp     word ptr [bx-62], 10
        je      short atk_b_hit
atk_b_n3:
        mov     ax, cx
        sub     ax, 18
        test    al, 88h
        jne     short atk_b_n4
        cmp     word ptr [bx-36], 10
        je      short atk_b_hit

        jmp     short atk_b_go
atk_b_hit:
        jmp     atk_hit
atk_b_go:
atk_b_n4:
        mov     ax, cx
        sub     ax, 14
        test    al, 88h
        jne     short atk_b_n5
        cmp     word ptr [bx-28], 10
        je      short atk_b_hit
atk_b_n5:
        mov     ax, cx
        add     ax, 14
        test    al, 88h
        jne     short atk_b_n6
        cmp     word ptr [bx+28], 10
        je      short atk_b_hit
atk_b_n6:
        mov     ax, cx
        add     ax, 18
        test    al, 88h
        jne     short atk_b_n7
        cmp     word ptr [bx+36], 10
        je      short atk_b_hit
atk_b_n7:
        mov     ax, cx
        add     ax, 31
        test    al, 88h
        jne     short atk_b_n8
        cmp     word ptr [bx+62], 10
        je      short atk_b_hit
atk_b_n8:
        mov     ax, cx
        add     ax, 33
        test    al, 88h
        jne     short atk_b_king
        cmp     word ptr [bx+66], 10
        je      short atk_b_hit

atk_b_king:
        mov     ax, [di+264]            ; to = p->ks[1]
        test    ax, ax                  ; unset king (diagnostic FEN): no attack
        js      short atk_b_rays
        mov     bx, ax
        shl     bx, 1
        add     bx, di
        cmp     word ptr [bx], 14       ; BK
        jne     short atk_b_rays
        sub     ax, cx
        mov     bx, ax
        mov     bh, 0
        test    byte ptr ss:_king_line[bx], 2
        jne     short atk_b_hit

atk_b_rays:
        RAY     -16, -32, 12, 13        ; north
        RAY       1,   2, 12, 13        ; east
        RAY      16,  32, 12, 13        ; south
        RAY      -1,  -2, 12, 13        ; west
        RAY     -17, -34, 11, 13        ; NE (SW/SE already scanned above)
        RAY     -15, -30, 11, 13        ; NW
; fall through: nothing attacks this square
        xor     ax, ax
        pop     di
        ret

; ---------------------------------------------------------------- result
atk_hit:
        mov     ax, 1
        pop     di
        ret
is_attacked_asm_ ENDP

_TEXT   ENDS
        END
