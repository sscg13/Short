; nnue_plan.asm - hand-written feature-row planning for the 16-bit target
;
; WHY THIS EXISTS.  nnue_make records, per perspective, the weight rows the
; child accumulator will need (mover at 'to', mover at 'from', the captured
; piece at 'to' or on the en-passant square).  That is six table lookups and
; eight stores, and the compiled C body pays for it several times over:
;
;   * every Pos/Undo member is a large-model far-pointer reload, and the
;     position is read five times (board[to], side, ks[0], ks[1], u->cap);
;   * every local lives in the stack frame and is re-read at -0 (no
;     optimisation), so `pm[persp]`/`mpost[persp]` cost a load per use;
;   * each lookup went through nn_row() - a call plus a branch chain that
;     folds perspective, colour and mirror into the table index.
;
; Measured in situ on the Nimbus 80186 (bench 4, 1.285 makes per node):
; 2,808 cycles per make, 11.2% of the depth-4 search, of which the perspective
; loop (the six lookups and eight stores) is 1,450 and the rest is entry,
; preamble and the mirror-flip rebuilds.
;
; WHAT THIS DOES DIFFERENTLY.
;
;   * the move is decoded once: from/to are c2sq(c) = c + (c & 56), and the
;     flags are one SHR by 12;
;   * the perspective/mirror index is folded ONCE into a 1 KB word table,
;     nn_cidxw[pm][sq88] = (compact(sq) ^ cm(pm)) * 2, where
;     pm = perspective | (mirror << 1) and cm = {0, 63, 7, 56}.  A row lookup
;     is then four instructions and three memory reads:
;           mov  ax, [bx+di]     ; c2 = nn_cidxw[pm][sq]   (di = 2*sq)
;           add  ax, poff        ; poff = rowtab + piece*128
;           mov  di, ax
;           mov  ax, [di]        ; row
;   * poff is precomputed only for the three pieces a make can touch (new piece
;     at 'to', mover at 'from', captured piece).  The black POV's colour swap
;     (pc ^ 8) is exactly XOR 1024 on poff, so the second perspective costs
;     nothing extra;
;   * both plan slots are written through one pointer that advances by 8, so
;     there is no per-perspective address multiply.
;
; CALLING CONVENTION (OpenWatcom 16-bit large model, __watcall):
;   ax = p->off, dx = p->seg (unused: the board is read through SS), bx = move,
;   cx = u->cap (the Undo's first word, read by the caller so this routine
;   never touches a far pointer).
;   Returns ax = flip mask; bit0/bit1 mean that perspective must be rebuilt
;   from scratch (nn_valid[nn_ply+1] is set by the caller from this).
; Writes nn_plan[nn_ply+1][0] and [1].  Preserves bx, si, di, bp, ds;
; clobbers ax, cx, dx.
;
; The C body in nnue.c stays as the oracle: build with build.ps1 -NoAsmPlan to
; compile it instead, and `nn` (the NNUE self-test) diffs the two on every
; legal move of a set of positions when both are built.
;
; SS == DS == DGROUP in this binary (the generated nnue_opt.asm rows rely on
; the same thing), so DS is set from SS at entry and restored on exit, and all
; near data is addressed without segment overrides.
;
; Pos layout (engine.h): board[128] at 0, side at 256, castle 258, ep 260,
; ks[2] at 262.  Undo: cap at 0.  Piece codes: WP 1 WN 2 WB 3 WR 4 WQ 5 WK 6,
; BP 9 BN 10 BB 11 BR 12 BQ 13 BK 14.  Move flags: MF_PROMO_N..Q = 1..4,
; MF_EP = 5, MF_CASTLE = 6.  Kingside castle targets are e1->g1 (0x06) and
; e8->g8 (0x76).

        .8086
        NAME    nnue_plan

_TEXT   SEGMENT BYTE PUBLIC USE16 'CODE'
        ASSUME  CS:_TEXT

        EXTRN   _nn_plan:WORD           ; [MAXPLY+1][2][4] words
        EXTRN   _nn_ply:WORD
        EXTRN   _nn_rowtab:WORD         ; [15][64] rows
        EXTRN   _nn_cidxw:WORD          ; [4][128] compact*2 per pm

        PUBLIC  nn_plan_slot_

; Stack frame (all words).
to          EQU     -2
fr          EQU     -4
newp        EQU     -6
mover       EQU     -8
mover_col   EQU     -10
flags       EQU     -12
cap_pc      EQU     -14
cap_sq      EQU     -16
flip        EQU     -18
poff_newp0  EQU     -20
poff_mover0 EQU     -22
poff_cap0   EQU     -24
plan_ptr    EQU     -26
cidx0       EQU     -28
cidx1       EQU     -30
ucap        EQU     -32
pm0         EQU     -34
pm1         EQU     -36
poff_newp1  EQU     -38
poff_mover1 EQU     -40
poff_cap1   EQU     -42
FRAME       EQU     48

; ---------------------------------------------------------------------------
; One perspective's plan slot.
;   persp = 0 or 1, selecting: the plan half (8 bytes apart), the cidx page,
;   the poff triple, the flip bit, the pm word, and the colour swap on poff.
;   The castle case index counts perspectives from the mover's own side, so
;   persp 1 complements mover_col.
; ---------------------------------------------------------------------------
PLAN    MACRO   persp
        LOCAL   done, castle, nocap, nocap2, ks
        mov     si, [bp+plan_ptr]
        IF      persp EQ 1
        add     si, 8
        ENDIF
        mov     bx, [bp+cidx0]
        IF      persp EQ 1
        mov     bx, [bp+cidx1]
        ENDIF
        mov     ax, [bp+flip]
        IF      persp EQ 1
        test    ax, 2
        ELSE
        test    ax, 1
        ENDIF
        jnz     done
        cmp     word ptr [bp+flags], 6
        je      short castle

; --- normal move: to_row, from_row, then (optional) cap_row ---------------
        mov     di, [bp+to]
        add     di, di
        mov     ax, [bx+di]
        IF      persp EQ 1
        add     ax, [bp+poff_newp1]
        ELSE
        add     ax, [bp+poff_newp0]
        ENDIF
        mov     di, ax
        mov     ax, [di]
        mov     [si+2], ax
        mov     di, [bp+fr]
        add     di, di
        mov     ax, [bx+di]
        IF      persp EQ 1
        add     ax, [bp+poff_mover1]
        ELSE
        add     ax, [bp+poff_mover0]
        ENDIF
        mov     di, ax
        mov     ax, [di]
        mov     [si+4], ax
        IF      persp EQ 1
        mov     ax, [bp+poff_cap1]
        ELSE
        mov     ax, [bp+poff_cap0]
        ENDIF
        test    ax, ax
        jz      short nocap
        mov     di, [bp+cap_sq]
        add     di, di
        mov     ax, [bx+di]
        IF      persp EQ 1
        add     ax, [bp+poff_cap1]
        ELSE
        add     ax, [bp+poff_cap0]
        ENDIF
        mov     di, ax
        mov     ax, [di]
        mov     [si+6], ax
        cmp     ax, -1
        je      short nocap2
        mov     word ptr [si], 2        ; kind 2: capture
        jmp     done
nocap:
        mov     word ptr [si+6], -1
nocap2:
        mov     word ptr [si], 1        ; kind 1: add/sub
        jmp     done

; --- castle: precomputed delta (kind 3) ----------------------------------
castle:
        mov     word ptr [si], 3
        mov     ax, [bp+mover_col]
        mov     [si+4], ax              ; b = mover's colour
        IF      persp EQ 1
        mov     ax, [bp+pm1]
        ELSE
        mov     ax, [bp+pm0]
        ENDIF
        shr     ax, 1
        mov     [si+6], ax              ; c = mirror of THIS perspective
        mov     ax, [bp+to]
        cmp     ax, 006h                ; e1->g1
        je      short ks
        cmp     ax, 076h                ; e8->g8
        je      short ks
        mov     word ptr [si+2], 2      ; queenside: only the nstm POV
        jmp     done
ks:
        mov     ax, [bp+mover_col]      ; own POV -> case 0
        IF      persp EQ 1
        xor     ax, 1
        ENDIF
        mov     [si+2], ax
done:
        ENDM

nn_plan_slot_   PROC FAR
        push    bp
        push    si
        push    di
        push    bx
        push    ds
        mov     bp, sp
        sub     sp, FRAME
        mov     si, ax                  ; si = p->off
        mov     ax, ss
        mov     ds, ax                  ; DS = DGROUP (SS); restored on exit
        mov     [bp+ucap], cx           ; u->cap

; --- move decode ---------------------------------------------------------
        mov     ax, bx
        and     ax, 63                  ; from, compact
        mov     dx, ax
        and     dx, 56
        add     ax, dx                  ; c2sq
        mov     [bp+fr], ax
        mov     ax, bx
        mov     cl, 6
        shr     ax, cl
        and     ax, 63                  ; to, compact
        mov     dx, ax
        and     dx, 56
        add     ax, dx
        mov     [bp+to], ax
        mov     ax, bx
        mov     cl, 12
        shr     ax, cl
        mov     [bp+flags], ax
        mov     bx, [bp+to]
        add     bx, bx
        add     bx, si
        mov     ax, [bx]                ; newp = board[to]
        mov     [bp+newp], ax
        mov     [bp+mover], ax
        mov     bx, [si+256]            ; side
        xor     bx, 1                   ; colour that just moved
        mov     [bp+mover_col], bx
        mov     ax, [bp+flags]          ; promotion? flags 1..4
        cmp     ax, 1
        jb      short pl_nopromo
        cmp     ax, 4
        ja      short pl_nopromo
        mov     ax, 1                   ; WP
        cmp     word ptr [bp+mover_col], 0
        je      short pl_pawn
        mov     ax, 9                   ; BP
pl_pawn:
        mov     [bp+mover], ax

; --- perspective/mirror index (pm = persp | mirror<<1) -------------------
pl_nopromo:
        mov     ax, [si+262]            ; ks[0]: white mirror = king on e-h
        and     ax, 4
        shr     ax, 1                   ; 0 or 2  (mirror << 1)
        mov     [bp+pm0], ax
        mov     cx, ax
        mov     ax, [si+264]            ; ks[1]: black mirror is inverted
        and     ax, 4
        xor     ax, 4
        shr     ax, 1
        inc     ax                      ; 3 or 1  (persp | mirror << 1)
        mov     [bp+pm1], ax

; --- mirror flip: only a king move can change a flag --------------------
        xor     dx, dx
        mov     bx, [bp+mover]
        cmp     bx, 6                   ; WK
        jne     short pl_chkbk
        mov     bx, [bp+fr]
        and     bx, 4
        shr     bx, 1                   ; parent white mirror << 1
        cmp     bx, cx                  ; cx = pm0
        je      short pl_flipdone
        mov     dx, 1
        jmp     short pl_flipdone
pl_chkbk:
        cmp     bx, 14                  ; BK
        jne     short pl_flipdone
        mov     bx, [bp+fr]
        and     bx, 4
        xor     bx, 4
        shr     bx, 1
        inc     bx                      ; parent pm1
        cmp     bx, ax                  ; ax = pm1
        je      short pl_flipdone
        mov     dx, 2
pl_flipdone:
        mov     [bp+flip], dx

; --- capture target (victim square and piece depend on the mover only) ---
        mov     ax, [bp+flags]
        cmp     ax, 5                   ; MF_EP
        jne     short pl_noep
        mov     ax, 9                   ; BP: white took a black pawn
        cmp     word ptr [bp+mover_col], 0
        je      short pl_eppawn
        mov     ax, 1                   ; WP
pl_eppawn:
        mov     [bp+cap_pc], ax
        mov     ax, [bp+to]
        cmp     word ptr [bp+mover_col], 0
        je      short pl_epw
        add     ax, 16
        jmp     short pl_epsq
pl_epw:
        sub     ax, 16
pl_epsq:
        mov     [bp+cap_sq], ax
        jmp     short pl_capoff
pl_noep:
        mov     ax, [bp+ucap]
        mov     [bp+cap_pc], ax
        mov     ax, [bp+to]
        mov     [bp+cap_sq], ax
pl_capoff:

; --- poff = rowtab + piece*128, for both perspectives --------------------
; (the black POV swaps colours, so its piece is pc ^ 8; that is an index
;  shift, NOT an xor of the address - the table is not 2 KB aligned)
        mov     bx, [bp+newp]
        mov     cl, 7
        shl     bx, cl
        add     bx, OFFSET _nn_rowtab
        mov     [bp+poff_newp0], bx
        mov     bx, [bp+newp]
        xor     bx, 8
        mov     cl, 7
        shl     bx, cl
        add     bx, OFFSET _nn_rowtab
        mov     [bp+poff_newp1], bx
        mov     bx, [bp+mover]
        mov     cl, 7
        shl     bx, cl
        add     bx, OFFSET _nn_rowtab
        mov     [bp+poff_mover0], bx
        mov     bx, [bp+mover]
        xor     bx, 8
        mov     cl, 7
        shl     bx, cl
        add     bx, OFFSET _nn_rowtab
        mov     [bp+poff_mover1], bx
        xor     ax, ax
        xor     bx, bx
        mov     dx, [bp+cap_pc]
        test    dx, dx
        jz      short pl_nocapoff
        mov     cl, 7
        mov     bx, dx
        shl     bx, cl
        add     bx, OFFSET _nn_rowtab
        xor     dx, 8
        mov     ax, dx
        shl     ax, cl
        add     ax, OFFSET _nn_rowtab
pl_nocapoff:
        mov     [bp+poff_cap0], bx
        mov     [bp+poff_cap1], ax

; --- per-perspective compact-square pages and the plan write pointer -----
        mov     bx, [bp+pm0]
        mov     cl, 8                   ; pm * 128 words
        shl     bx, cl
        add     bx, OFFSET _nn_cidxw
        mov     [bp+cidx0], bx
        mov     bx, [bp+pm1]
        mov     cl, 8
        shl     bx, cl
        add     bx, OFFSET _nn_cidxw
        mov     [bp+cidx1], bx
        mov     bx, ss:_nn_ply
        inc     bx
        mov     cl, 4                   ; sizeof(nn_plan[slot][2])
        shl     bx, cl
        add     bx, OFFSET _nn_plan
        mov     [bp+plan_ptr], bx

        PLAN    0
        PLAN    1

        mov     ax, [bp+flip]
        mov     sp, bp
        pop     ds
        pop     bx
        pop     di
        pop     si
        pop     bp
        retf
nn_plan_slot_   ENDP

_TEXT   ENDS
        END
