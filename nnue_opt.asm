; nnue_opt.asm - hand-unrolled 64-element NNUE apply loops (8088 target).
;
; Replaces the compiler's 64-iteration loop - which reloads DS to the far weight
; segment on EVERY element, increments two pointers, and pays a backward-branch
; prefetch-queue flush (the tight-loop refetch) each iteration - with fully
; unrolled straight-line code using constant disp8 addressing:
;     mov al, [si+d]       ; weight byte (DS = weight segment)
;     cbw                  ; sign-extend to i16
;     add es:[di+2d], ax   ; acc word RMW (ES = DGROUP), d=0..63, 2d<=126
; Both address spans fit disp8, so: no loop, no increments, no per-element DS
; reload, no prefetch-queue flush. ~1.0 KB of code; the two bodies are reused by
; make AND undo (a recorded add is reversed with sub, a recorded sub with add).
;
; Calling convention (OpenWatcom 16-bit, large model): FAR proc, first arg in
; ax (persp), second in dx (row). Preserves bx, si, di, bp, ds; clobbers
; ax, dx, cx, es (all caller-saved in Watcom 16-bit).
;
        .8086
        NAME    nnue_opt

_TEXT   SEGMENT BYTE PUBLIC USE16 'CODE'
        ASSUME  CS:_TEXT

        EXTRN   _nn_w1:BYTE
        EXTRN   _nn_acc:WORD
        EXTRN   _nn_ply:WORD
        EXTRN   _nn_fwd:WORD
        EXTRN   _nn_w2:BYTE
        EXTRN   _nn_bias:WORD

        PUBLIC  nn_apply_add_
        PUBLIC  nn_apply_sub_
        PUBLIC  nn_make_move_
        PUBLIC  nn_make_cap_
        PUBLIC  nn_fwd_eval_
        PUBLIC  nn_fwd_eval2_
nn_apply_add_ PROC FAR
        push    bx
        push    si
        push    di
        push    ds
        mov     si, dx
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        mov     bx, ax
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        mov     ax, SEG _nn_w1
        mov     ds, ax
        add     si, OFFSET _nn_w1
        mov     ax, ss
        mov     es, ax
        mov     di, OFFSET _nn_acc
        add     di, bx
        mov     al, [si+0]
        cbw
        add     es:[di+0], ax
        mov     al, [si+1]
        cbw
        add     es:[di+2], ax
        mov     al, [si+2]
        cbw
        add     es:[di+4], ax
        mov     al, [si+3]
        cbw
        add     es:[di+6], ax
        mov     al, [si+4]
        cbw
        add     es:[di+8], ax
        mov     al, [si+5]
        cbw
        add     es:[di+10], ax
        mov     al, [si+6]
        cbw
        add     es:[di+12], ax
        mov     al, [si+7]
        cbw
        add     es:[di+14], ax
        mov     al, [si+8]
        cbw
        add     es:[di+16], ax
        mov     al, [si+9]
        cbw
        add     es:[di+18], ax
        mov     al, [si+10]
        cbw
        add     es:[di+20], ax
        mov     al, [si+11]
        cbw
        add     es:[di+22], ax
        mov     al, [si+12]
        cbw
        add     es:[di+24], ax
        mov     al, [si+13]
        cbw
        add     es:[di+26], ax
        mov     al, [si+14]
        cbw
        add     es:[di+28], ax
        mov     al, [si+15]
        cbw
        add     es:[di+30], ax
        mov     al, [si+16]
        cbw
        add     es:[di+32], ax
        mov     al, [si+17]
        cbw
        add     es:[di+34], ax
        mov     al, [si+18]
        cbw
        add     es:[di+36], ax
        mov     al, [si+19]
        cbw
        add     es:[di+38], ax
        mov     al, [si+20]
        cbw
        add     es:[di+40], ax
        mov     al, [si+21]
        cbw
        add     es:[di+42], ax
        mov     al, [si+22]
        cbw
        add     es:[di+44], ax
        mov     al, [si+23]
        cbw
        add     es:[di+46], ax
        mov     al, [si+24]
        cbw
        add     es:[di+48], ax
        mov     al, [si+25]
        cbw
        add     es:[di+50], ax
        mov     al, [si+26]
        cbw
        add     es:[di+52], ax
        mov     al, [si+27]
        cbw
        add     es:[di+54], ax
        mov     al, [si+28]
        cbw
        add     es:[di+56], ax
        mov     al, [si+29]
        cbw
        add     es:[di+58], ax
        mov     al, [si+30]
        cbw
        add     es:[di+60], ax
        mov     al, [si+31]
        cbw
        add     es:[di+62], ax
        mov     al, [si+32]
        cbw
        add     es:[di+64], ax
        mov     al, [si+33]
        cbw
        add     es:[di+66], ax
        mov     al, [si+34]
        cbw
        add     es:[di+68], ax
        mov     al, [si+35]
        cbw
        add     es:[di+70], ax
        mov     al, [si+36]
        cbw
        add     es:[di+72], ax
        mov     al, [si+37]
        cbw
        add     es:[di+74], ax
        mov     al, [si+38]
        cbw
        add     es:[di+76], ax
        mov     al, [si+39]
        cbw
        add     es:[di+78], ax
        mov     al, [si+40]
        cbw
        add     es:[di+80], ax
        mov     al, [si+41]
        cbw
        add     es:[di+82], ax
        mov     al, [si+42]
        cbw
        add     es:[di+84], ax
        mov     al, [si+43]
        cbw
        add     es:[di+86], ax
        mov     al, [si+44]
        cbw
        add     es:[di+88], ax
        mov     al, [si+45]
        cbw
        add     es:[di+90], ax
        mov     al, [si+46]
        cbw
        add     es:[di+92], ax
        mov     al, [si+47]
        cbw
        add     es:[di+94], ax
        mov     al, [si+48]
        cbw
        add     es:[di+96], ax
        mov     al, [si+49]
        cbw
        add     es:[di+98], ax
        mov     al, [si+50]
        cbw
        add     es:[di+100], ax
        mov     al, [si+51]
        cbw
        add     es:[di+102], ax
        mov     al, [si+52]
        cbw
        add     es:[di+104], ax
        mov     al, [si+53]
        cbw
        add     es:[di+106], ax
        mov     al, [si+54]
        cbw
        add     es:[di+108], ax
        mov     al, [si+55]
        cbw
        add     es:[di+110], ax
        mov     al, [si+56]
        cbw
        add     es:[di+112], ax
        mov     al, [si+57]
        cbw
        add     es:[di+114], ax
        mov     al, [si+58]
        cbw
        add     es:[di+116], ax
        mov     al, [si+59]
        cbw
        add     es:[di+118], ax
        mov     al, [si+60]
        cbw
        add     es:[di+120], ax
        mov     al, [si+61]
        cbw
        add     es:[di+122], ax
        mov     al, [si+62]
        cbw
        add     es:[di+124], ax
        mov     al, [si+63]
        cbw
        add     es:[di+126], ax
        pop     ds
        pop     di
        pop     si
        pop     bx
        retf
nn_apply_add_ ENDP

nn_apply_sub_ PROC FAR
        push    bx
        push    si
        push    di
        push    ds
        mov     si, dx
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        mov     bx, ax
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        mov     ax, SEG _nn_w1
        mov     ds, ax
        add     si, OFFSET _nn_w1
        mov     ax, ss
        mov     es, ax
        mov     di, OFFSET _nn_acc
        add     di, bx
        mov     al, [si+0]
        cbw
        sub     es:[di+0], ax
        mov     al, [si+1]
        cbw
        sub     es:[di+2], ax
        mov     al, [si+2]
        cbw
        sub     es:[di+4], ax
        mov     al, [si+3]
        cbw
        sub     es:[di+6], ax
        mov     al, [si+4]
        cbw
        sub     es:[di+8], ax
        mov     al, [si+5]
        cbw
        sub     es:[di+10], ax
        mov     al, [si+6]
        cbw
        sub     es:[di+12], ax
        mov     al, [si+7]
        cbw
        sub     es:[di+14], ax
        mov     al, [si+8]
        cbw
        sub     es:[di+16], ax
        mov     al, [si+9]
        cbw
        sub     es:[di+18], ax
        mov     al, [si+10]
        cbw
        sub     es:[di+20], ax
        mov     al, [si+11]
        cbw
        sub     es:[di+22], ax
        mov     al, [si+12]
        cbw
        sub     es:[di+24], ax
        mov     al, [si+13]
        cbw
        sub     es:[di+26], ax
        mov     al, [si+14]
        cbw
        sub     es:[di+28], ax
        mov     al, [si+15]
        cbw
        sub     es:[di+30], ax
        mov     al, [si+16]
        cbw
        sub     es:[di+32], ax
        mov     al, [si+17]
        cbw
        sub     es:[di+34], ax
        mov     al, [si+18]
        cbw
        sub     es:[di+36], ax
        mov     al, [si+19]
        cbw
        sub     es:[di+38], ax
        mov     al, [si+20]
        cbw
        sub     es:[di+40], ax
        mov     al, [si+21]
        cbw
        sub     es:[di+42], ax
        mov     al, [si+22]
        cbw
        sub     es:[di+44], ax
        mov     al, [si+23]
        cbw
        sub     es:[di+46], ax
        mov     al, [si+24]
        cbw
        sub     es:[di+48], ax
        mov     al, [si+25]
        cbw
        sub     es:[di+50], ax
        mov     al, [si+26]
        cbw
        sub     es:[di+52], ax
        mov     al, [si+27]
        cbw
        sub     es:[di+54], ax
        mov     al, [si+28]
        cbw
        sub     es:[di+56], ax
        mov     al, [si+29]
        cbw
        sub     es:[di+58], ax
        mov     al, [si+30]
        cbw
        sub     es:[di+60], ax
        mov     al, [si+31]
        cbw
        sub     es:[di+62], ax
        mov     al, [si+32]
        cbw
        sub     es:[di+64], ax
        mov     al, [si+33]
        cbw
        sub     es:[di+66], ax
        mov     al, [si+34]
        cbw
        sub     es:[di+68], ax
        mov     al, [si+35]
        cbw
        sub     es:[di+70], ax
        mov     al, [si+36]
        cbw
        sub     es:[di+72], ax
        mov     al, [si+37]
        cbw
        sub     es:[di+74], ax
        mov     al, [si+38]
        cbw
        sub     es:[di+76], ax
        mov     al, [si+39]
        cbw
        sub     es:[di+78], ax
        mov     al, [si+40]
        cbw
        sub     es:[di+80], ax
        mov     al, [si+41]
        cbw
        sub     es:[di+82], ax
        mov     al, [si+42]
        cbw
        sub     es:[di+84], ax
        mov     al, [si+43]
        cbw
        sub     es:[di+86], ax
        mov     al, [si+44]
        cbw
        sub     es:[di+88], ax
        mov     al, [si+45]
        cbw
        sub     es:[di+90], ax
        mov     al, [si+46]
        cbw
        sub     es:[di+92], ax
        mov     al, [si+47]
        cbw
        sub     es:[di+94], ax
        mov     al, [si+48]
        cbw
        sub     es:[di+96], ax
        mov     al, [si+49]
        cbw
        sub     es:[di+98], ax
        mov     al, [si+50]
        cbw
        sub     es:[di+100], ax
        mov     al, [si+51]
        cbw
        sub     es:[di+102], ax
        mov     al, [si+52]
        cbw
        sub     es:[di+104], ax
        mov     al, [si+53]
        cbw
        sub     es:[di+106], ax
        mov     al, [si+54]
        cbw
        sub     es:[di+108], ax
        mov     al, [si+55]
        cbw
        sub     es:[di+110], ax
        mov     al, [si+56]
        cbw
        sub     es:[di+112], ax
        mov     al, [si+57]
        cbw
        sub     es:[di+114], ax
        mov     al, [si+58]
        cbw
        sub     es:[di+116], ax
        mov     al, [si+59]
        cbw
        sub     es:[di+118], ax
        mov     al, [si+60]
        cbw
        sub     es:[di+120], ax
        mov     al, [si+61]
        cbw
        sub     es:[di+122], ax
        mov     al, [si+62]
        cbw
        sub     es:[di+124], ax
        mov     al, [si+63]
        cbw
        sub     es:[di+126], ax
        pop     ds
        pop     di
        pop     si
        pop     bx
        retf
nn_apply_sub_ ENDP

; =====================================================================
; nn_make_move_ - batched PLY-INDEXED NNUE make for a NORMAL move
; (quiet or non-capture promo): nn_acc[ply+1][j] = nn_acc[ply][j] +
; w1[to_row*64+j] - w1[from_row*64+j] in ONE pass, writing the CHILD slot
; of the accumulator stack so undo is a nn_ply decrement (no 256-byte
; restore memcpy). src = slot nn_ply (via the _nn_ply global), dst = the
; slot 256 bytes after it. Calling convention: FAR, ax=persp, dx=to_row,
; bx=from_row. Preserves bx,bp,si,di,ds; clobbers ax,dx,cx,es. Bit-identical
; to the in-place RMW (i16 arithmetic mod 2^16).
; GENERATED by gen_nnue_batch.py - edit the script, not this block.
nn_make_move_ PROC FAR
        push    bx
        push    bp
        push    si
        push    di
        push    ds
        mov     si, dx
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        mov     di, OFFSET _nn_acc
        mov     cx, ss:_nn_ply
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        add     di, cx
        mov     bp, di
        add     di, 256
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        add     bp, ax
        add     di, ax
        mov     ax, SEG _nn_w1
        mov     ds, ax
        add     si, OFFSET _nn_w1
        add     bx, OFFSET _nn_w1
        mov     ax, ss
        mov     es, ax
        mov     al, [si+0]
        cbw
        mov     dx, ax
        mov     al, [bx+0]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+0]
        add     ax, dx
        mov     es:[di+0], ax
        mov     al, [si+1]
        cbw
        mov     dx, ax
        mov     al, [bx+1]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+2]
        add     ax, dx
        mov     es:[di+2], ax
        mov     al, [si+2]
        cbw
        mov     dx, ax
        mov     al, [bx+2]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+4]
        add     ax, dx
        mov     es:[di+4], ax
        mov     al, [si+3]
        cbw
        mov     dx, ax
        mov     al, [bx+3]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+6]
        add     ax, dx
        mov     es:[di+6], ax
        mov     al, [si+4]
        cbw
        mov     dx, ax
        mov     al, [bx+4]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+8]
        add     ax, dx
        mov     es:[di+8], ax
        mov     al, [si+5]
        cbw
        mov     dx, ax
        mov     al, [bx+5]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+10]
        add     ax, dx
        mov     es:[di+10], ax
        mov     al, [si+6]
        cbw
        mov     dx, ax
        mov     al, [bx+6]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+12]
        add     ax, dx
        mov     es:[di+12], ax
        mov     al, [si+7]
        cbw
        mov     dx, ax
        mov     al, [bx+7]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+14]
        add     ax, dx
        mov     es:[di+14], ax
        mov     al, [si+8]
        cbw
        mov     dx, ax
        mov     al, [bx+8]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+16]
        add     ax, dx
        mov     es:[di+16], ax
        mov     al, [si+9]
        cbw
        mov     dx, ax
        mov     al, [bx+9]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+18]
        add     ax, dx
        mov     es:[di+18], ax
        mov     al, [si+10]
        cbw
        mov     dx, ax
        mov     al, [bx+10]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+20]
        add     ax, dx
        mov     es:[di+20], ax
        mov     al, [si+11]
        cbw
        mov     dx, ax
        mov     al, [bx+11]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+22]
        add     ax, dx
        mov     es:[di+22], ax
        mov     al, [si+12]
        cbw
        mov     dx, ax
        mov     al, [bx+12]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+24]
        add     ax, dx
        mov     es:[di+24], ax
        mov     al, [si+13]
        cbw
        mov     dx, ax
        mov     al, [bx+13]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+26]
        add     ax, dx
        mov     es:[di+26], ax
        mov     al, [si+14]
        cbw
        mov     dx, ax
        mov     al, [bx+14]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+28]
        add     ax, dx
        mov     es:[di+28], ax
        mov     al, [si+15]
        cbw
        mov     dx, ax
        mov     al, [bx+15]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+30]
        add     ax, dx
        mov     es:[di+30], ax
        mov     al, [si+16]
        cbw
        mov     dx, ax
        mov     al, [bx+16]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+32]
        add     ax, dx
        mov     es:[di+32], ax
        mov     al, [si+17]
        cbw
        mov     dx, ax
        mov     al, [bx+17]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+34]
        add     ax, dx
        mov     es:[di+34], ax
        mov     al, [si+18]
        cbw
        mov     dx, ax
        mov     al, [bx+18]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+36]
        add     ax, dx
        mov     es:[di+36], ax
        mov     al, [si+19]
        cbw
        mov     dx, ax
        mov     al, [bx+19]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+38]
        add     ax, dx
        mov     es:[di+38], ax
        mov     al, [si+20]
        cbw
        mov     dx, ax
        mov     al, [bx+20]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+40]
        add     ax, dx
        mov     es:[di+40], ax
        mov     al, [si+21]
        cbw
        mov     dx, ax
        mov     al, [bx+21]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+42]
        add     ax, dx
        mov     es:[di+42], ax
        mov     al, [si+22]
        cbw
        mov     dx, ax
        mov     al, [bx+22]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+44]
        add     ax, dx
        mov     es:[di+44], ax
        mov     al, [si+23]
        cbw
        mov     dx, ax
        mov     al, [bx+23]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+46]
        add     ax, dx
        mov     es:[di+46], ax
        mov     al, [si+24]
        cbw
        mov     dx, ax
        mov     al, [bx+24]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+48]
        add     ax, dx
        mov     es:[di+48], ax
        mov     al, [si+25]
        cbw
        mov     dx, ax
        mov     al, [bx+25]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+50]
        add     ax, dx
        mov     es:[di+50], ax
        mov     al, [si+26]
        cbw
        mov     dx, ax
        mov     al, [bx+26]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+52]
        add     ax, dx
        mov     es:[di+52], ax
        mov     al, [si+27]
        cbw
        mov     dx, ax
        mov     al, [bx+27]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+54]
        add     ax, dx
        mov     es:[di+54], ax
        mov     al, [si+28]
        cbw
        mov     dx, ax
        mov     al, [bx+28]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+56]
        add     ax, dx
        mov     es:[di+56], ax
        mov     al, [si+29]
        cbw
        mov     dx, ax
        mov     al, [bx+29]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+58]
        add     ax, dx
        mov     es:[di+58], ax
        mov     al, [si+30]
        cbw
        mov     dx, ax
        mov     al, [bx+30]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+60]
        add     ax, dx
        mov     es:[di+60], ax
        mov     al, [si+31]
        cbw
        mov     dx, ax
        mov     al, [bx+31]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+62]
        add     ax, dx
        mov     es:[di+62], ax
        mov     al, [si+32]
        cbw
        mov     dx, ax
        mov     al, [bx+32]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+64]
        add     ax, dx
        mov     es:[di+64], ax
        mov     al, [si+33]
        cbw
        mov     dx, ax
        mov     al, [bx+33]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+66]
        add     ax, dx
        mov     es:[di+66], ax
        mov     al, [si+34]
        cbw
        mov     dx, ax
        mov     al, [bx+34]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+68]
        add     ax, dx
        mov     es:[di+68], ax
        mov     al, [si+35]
        cbw
        mov     dx, ax
        mov     al, [bx+35]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+70]
        add     ax, dx
        mov     es:[di+70], ax
        mov     al, [si+36]
        cbw
        mov     dx, ax
        mov     al, [bx+36]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+72]
        add     ax, dx
        mov     es:[di+72], ax
        mov     al, [si+37]
        cbw
        mov     dx, ax
        mov     al, [bx+37]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+74]
        add     ax, dx
        mov     es:[di+74], ax
        mov     al, [si+38]
        cbw
        mov     dx, ax
        mov     al, [bx+38]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+76]
        add     ax, dx
        mov     es:[di+76], ax
        mov     al, [si+39]
        cbw
        mov     dx, ax
        mov     al, [bx+39]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+78]
        add     ax, dx
        mov     es:[di+78], ax
        mov     al, [si+40]
        cbw
        mov     dx, ax
        mov     al, [bx+40]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+80]
        add     ax, dx
        mov     es:[di+80], ax
        mov     al, [si+41]
        cbw
        mov     dx, ax
        mov     al, [bx+41]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+82]
        add     ax, dx
        mov     es:[di+82], ax
        mov     al, [si+42]
        cbw
        mov     dx, ax
        mov     al, [bx+42]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+84]
        add     ax, dx
        mov     es:[di+84], ax
        mov     al, [si+43]
        cbw
        mov     dx, ax
        mov     al, [bx+43]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+86]
        add     ax, dx
        mov     es:[di+86], ax
        mov     al, [si+44]
        cbw
        mov     dx, ax
        mov     al, [bx+44]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+88]
        add     ax, dx
        mov     es:[di+88], ax
        mov     al, [si+45]
        cbw
        mov     dx, ax
        mov     al, [bx+45]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+90]
        add     ax, dx
        mov     es:[di+90], ax
        mov     al, [si+46]
        cbw
        mov     dx, ax
        mov     al, [bx+46]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+92]
        add     ax, dx
        mov     es:[di+92], ax
        mov     al, [si+47]
        cbw
        mov     dx, ax
        mov     al, [bx+47]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+94]
        add     ax, dx
        mov     es:[di+94], ax
        mov     al, [si+48]
        cbw
        mov     dx, ax
        mov     al, [bx+48]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+96]
        add     ax, dx
        mov     es:[di+96], ax
        mov     al, [si+49]
        cbw
        mov     dx, ax
        mov     al, [bx+49]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+98]
        add     ax, dx
        mov     es:[di+98], ax
        mov     al, [si+50]
        cbw
        mov     dx, ax
        mov     al, [bx+50]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+100]
        add     ax, dx
        mov     es:[di+100], ax
        mov     al, [si+51]
        cbw
        mov     dx, ax
        mov     al, [bx+51]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+102]
        add     ax, dx
        mov     es:[di+102], ax
        mov     al, [si+52]
        cbw
        mov     dx, ax
        mov     al, [bx+52]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+104]
        add     ax, dx
        mov     es:[di+104], ax
        mov     al, [si+53]
        cbw
        mov     dx, ax
        mov     al, [bx+53]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+106]
        add     ax, dx
        mov     es:[di+106], ax
        mov     al, [si+54]
        cbw
        mov     dx, ax
        mov     al, [bx+54]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+108]
        add     ax, dx
        mov     es:[di+108], ax
        mov     al, [si+55]
        cbw
        mov     dx, ax
        mov     al, [bx+55]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+110]
        add     ax, dx
        mov     es:[di+110], ax
        mov     al, [si+56]
        cbw
        mov     dx, ax
        mov     al, [bx+56]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+112]
        add     ax, dx
        mov     es:[di+112], ax
        mov     al, [si+57]
        cbw
        mov     dx, ax
        mov     al, [bx+57]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+114]
        add     ax, dx
        mov     es:[di+114], ax
        mov     al, [si+58]
        cbw
        mov     dx, ax
        mov     al, [bx+58]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+116]
        add     ax, dx
        mov     es:[di+116], ax
        mov     al, [si+59]
        cbw
        mov     dx, ax
        mov     al, [bx+59]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+118]
        add     ax, dx
        mov     es:[di+118], ax
        mov     al, [si+60]
        cbw
        mov     dx, ax
        mov     al, [bx+60]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+120]
        add     ax, dx
        mov     es:[di+120], ax
        mov     al, [si+61]
        cbw
        mov     dx, ax
        mov     al, [bx+61]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+122]
        add     ax, dx
        mov     es:[di+122], ax
        mov     al, [si+62]
        cbw
        mov     dx, ax
        mov     al, [bx+62]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+124]
        add     ax, dx
        mov     es:[di+124], ax
        mov     al, [si+63]
        cbw
        mov     dx, ax
        mov     al, [bx+63]
        cbw
        sub     dx, ax
        mov     ax, es:[bp+126]
        add     ax, dx
        mov     es:[di+126], ax
        pop     ds
        pop     di
        pop     si
        pop     bp
        pop     bx
        retf
nn_make_move_ ENDP

; nn_make_cap_ - batched PLY-INDEXED NNUE make for a CAPTURE or EP:
; nn_acc[ply+1][j] = nn_acc[ply][j] + w1[to_row*64+j] - w1[from_row*64+j]
; - w1[cap_row*64+j] in ONE pass into the CHILD slot. The three w1 rows use
; si/bx/bp and dst uses di, so the src (slot nn_ply) is read as di - 256
; (a constant disp16 - the child slot is always 256 bytes past its parent).
; Calling convention: FAR, ax=persp, dx=to_row, bx=from_row, cx=cap_row.
; Preserves bx,bp,si,di,ds; clobbers ax,dx,cx,es.
; GENERATED by gen_nnue_batch.py - edit the script, not this block.
nn_make_cap_ PROC FAR
        push    bx
        push    bp
        push    si
        push    di
        push    ds
        mov     si, dx
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     si, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        shl     bx, 1
        mov     bp, cx
        shl     bp, 1
        shl     bp, 1
        shl     bp, 1
        shl     bp, 1
        shl     bp, 1
        shl     bp, 1
        mov     di, OFFSET _nn_acc
        mov     cx, ss:_nn_ply
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        add     di, cx
        add     di, 256
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        add     di, ax
        mov     ax, SEG _nn_w1
        mov     ds, ax
        add     si, OFFSET _nn_w1
        add     bx, OFFSET _nn_w1
        add     bp, OFFSET _nn_w1
        mov     ax, ss
        mov     es, ax
        mov     al, [si+0]
        cbw
        mov     dx, ax
        mov     al, [bx+0]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+0]
        cbw
        sub     dx, ax
        mov     ax, es:[di-256]
        add     ax, dx
        mov     es:[di+0], ax
        mov     al, [si+1]
        cbw
        mov     dx, ax
        mov     al, [bx+1]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+1]
        cbw
        sub     dx, ax
        mov     ax, es:[di-254]
        add     ax, dx
        mov     es:[di+2], ax
        mov     al, [si+2]
        cbw
        mov     dx, ax
        mov     al, [bx+2]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+2]
        cbw
        sub     dx, ax
        mov     ax, es:[di-252]
        add     ax, dx
        mov     es:[di+4], ax
        mov     al, [si+3]
        cbw
        mov     dx, ax
        mov     al, [bx+3]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+3]
        cbw
        sub     dx, ax
        mov     ax, es:[di-250]
        add     ax, dx
        mov     es:[di+6], ax
        mov     al, [si+4]
        cbw
        mov     dx, ax
        mov     al, [bx+4]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+4]
        cbw
        sub     dx, ax
        mov     ax, es:[di-248]
        add     ax, dx
        mov     es:[di+8], ax
        mov     al, [si+5]
        cbw
        mov     dx, ax
        mov     al, [bx+5]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+5]
        cbw
        sub     dx, ax
        mov     ax, es:[di-246]
        add     ax, dx
        mov     es:[di+10], ax
        mov     al, [si+6]
        cbw
        mov     dx, ax
        mov     al, [bx+6]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+6]
        cbw
        sub     dx, ax
        mov     ax, es:[di-244]
        add     ax, dx
        mov     es:[di+12], ax
        mov     al, [si+7]
        cbw
        mov     dx, ax
        mov     al, [bx+7]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+7]
        cbw
        sub     dx, ax
        mov     ax, es:[di-242]
        add     ax, dx
        mov     es:[di+14], ax
        mov     al, [si+8]
        cbw
        mov     dx, ax
        mov     al, [bx+8]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+8]
        cbw
        sub     dx, ax
        mov     ax, es:[di-240]
        add     ax, dx
        mov     es:[di+16], ax
        mov     al, [si+9]
        cbw
        mov     dx, ax
        mov     al, [bx+9]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+9]
        cbw
        sub     dx, ax
        mov     ax, es:[di-238]
        add     ax, dx
        mov     es:[di+18], ax
        mov     al, [si+10]
        cbw
        mov     dx, ax
        mov     al, [bx+10]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+10]
        cbw
        sub     dx, ax
        mov     ax, es:[di-236]
        add     ax, dx
        mov     es:[di+20], ax
        mov     al, [si+11]
        cbw
        mov     dx, ax
        mov     al, [bx+11]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+11]
        cbw
        sub     dx, ax
        mov     ax, es:[di-234]
        add     ax, dx
        mov     es:[di+22], ax
        mov     al, [si+12]
        cbw
        mov     dx, ax
        mov     al, [bx+12]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+12]
        cbw
        sub     dx, ax
        mov     ax, es:[di-232]
        add     ax, dx
        mov     es:[di+24], ax
        mov     al, [si+13]
        cbw
        mov     dx, ax
        mov     al, [bx+13]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+13]
        cbw
        sub     dx, ax
        mov     ax, es:[di-230]
        add     ax, dx
        mov     es:[di+26], ax
        mov     al, [si+14]
        cbw
        mov     dx, ax
        mov     al, [bx+14]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+14]
        cbw
        sub     dx, ax
        mov     ax, es:[di-228]
        add     ax, dx
        mov     es:[di+28], ax
        mov     al, [si+15]
        cbw
        mov     dx, ax
        mov     al, [bx+15]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+15]
        cbw
        sub     dx, ax
        mov     ax, es:[di-226]
        add     ax, dx
        mov     es:[di+30], ax
        mov     al, [si+16]
        cbw
        mov     dx, ax
        mov     al, [bx+16]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+16]
        cbw
        sub     dx, ax
        mov     ax, es:[di-224]
        add     ax, dx
        mov     es:[di+32], ax
        mov     al, [si+17]
        cbw
        mov     dx, ax
        mov     al, [bx+17]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+17]
        cbw
        sub     dx, ax
        mov     ax, es:[di-222]
        add     ax, dx
        mov     es:[di+34], ax
        mov     al, [si+18]
        cbw
        mov     dx, ax
        mov     al, [bx+18]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+18]
        cbw
        sub     dx, ax
        mov     ax, es:[di-220]
        add     ax, dx
        mov     es:[di+36], ax
        mov     al, [si+19]
        cbw
        mov     dx, ax
        mov     al, [bx+19]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+19]
        cbw
        sub     dx, ax
        mov     ax, es:[di-218]
        add     ax, dx
        mov     es:[di+38], ax
        mov     al, [si+20]
        cbw
        mov     dx, ax
        mov     al, [bx+20]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+20]
        cbw
        sub     dx, ax
        mov     ax, es:[di-216]
        add     ax, dx
        mov     es:[di+40], ax
        mov     al, [si+21]
        cbw
        mov     dx, ax
        mov     al, [bx+21]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+21]
        cbw
        sub     dx, ax
        mov     ax, es:[di-214]
        add     ax, dx
        mov     es:[di+42], ax
        mov     al, [si+22]
        cbw
        mov     dx, ax
        mov     al, [bx+22]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+22]
        cbw
        sub     dx, ax
        mov     ax, es:[di-212]
        add     ax, dx
        mov     es:[di+44], ax
        mov     al, [si+23]
        cbw
        mov     dx, ax
        mov     al, [bx+23]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+23]
        cbw
        sub     dx, ax
        mov     ax, es:[di-210]
        add     ax, dx
        mov     es:[di+46], ax
        mov     al, [si+24]
        cbw
        mov     dx, ax
        mov     al, [bx+24]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+24]
        cbw
        sub     dx, ax
        mov     ax, es:[di-208]
        add     ax, dx
        mov     es:[di+48], ax
        mov     al, [si+25]
        cbw
        mov     dx, ax
        mov     al, [bx+25]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+25]
        cbw
        sub     dx, ax
        mov     ax, es:[di-206]
        add     ax, dx
        mov     es:[di+50], ax
        mov     al, [si+26]
        cbw
        mov     dx, ax
        mov     al, [bx+26]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+26]
        cbw
        sub     dx, ax
        mov     ax, es:[di-204]
        add     ax, dx
        mov     es:[di+52], ax
        mov     al, [si+27]
        cbw
        mov     dx, ax
        mov     al, [bx+27]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+27]
        cbw
        sub     dx, ax
        mov     ax, es:[di-202]
        add     ax, dx
        mov     es:[di+54], ax
        mov     al, [si+28]
        cbw
        mov     dx, ax
        mov     al, [bx+28]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+28]
        cbw
        sub     dx, ax
        mov     ax, es:[di-200]
        add     ax, dx
        mov     es:[di+56], ax
        mov     al, [si+29]
        cbw
        mov     dx, ax
        mov     al, [bx+29]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+29]
        cbw
        sub     dx, ax
        mov     ax, es:[di-198]
        add     ax, dx
        mov     es:[di+58], ax
        mov     al, [si+30]
        cbw
        mov     dx, ax
        mov     al, [bx+30]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+30]
        cbw
        sub     dx, ax
        mov     ax, es:[di-196]
        add     ax, dx
        mov     es:[di+60], ax
        mov     al, [si+31]
        cbw
        mov     dx, ax
        mov     al, [bx+31]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+31]
        cbw
        sub     dx, ax
        mov     ax, es:[di-194]
        add     ax, dx
        mov     es:[di+62], ax
        mov     al, [si+32]
        cbw
        mov     dx, ax
        mov     al, [bx+32]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+32]
        cbw
        sub     dx, ax
        mov     ax, es:[di-192]
        add     ax, dx
        mov     es:[di+64], ax
        mov     al, [si+33]
        cbw
        mov     dx, ax
        mov     al, [bx+33]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+33]
        cbw
        sub     dx, ax
        mov     ax, es:[di-190]
        add     ax, dx
        mov     es:[di+66], ax
        mov     al, [si+34]
        cbw
        mov     dx, ax
        mov     al, [bx+34]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+34]
        cbw
        sub     dx, ax
        mov     ax, es:[di-188]
        add     ax, dx
        mov     es:[di+68], ax
        mov     al, [si+35]
        cbw
        mov     dx, ax
        mov     al, [bx+35]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+35]
        cbw
        sub     dx, ax
        mov     ax, es:[di-186]
        add     ax, dx
        mov     es:[di+70], ax
        mov     al, [si+36]
        cbw
        mov     dx, ax
        mov     al, [bx+36]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+36]
        cbw
        sub     dx, ax
        mov     ax, es:[di-184]
        add     ax, dx
        mov     es:[di+72], ax
        mov     al, [si+37]
        cbw
        mov     dx, ax
        mov     al, [bx+37]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+37]
        cbw
        sub     dx, ax
        mov     ax, es:[di-182]
        add     ax, dx
        mov     es:[di+74], ax
        mov     al, [si+38]
        cbw
        mov     dx, ax
        mov     al, [bx+38]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+38]
        cbw
        sub     dx, ax
        mov     ax, es:[di-180]
        add     ax, dx
        mov     es:[di+76], ax
        mov     al, [si+39]
        cbw
        mov     dx, ax
        mov     al, [bx+39]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+39]
        cbw
        sub     dx, ax
        mov     ax, es:[di-178]
        add     ax, dx
        mov     es:[di+78], ax
        mov     al, [si+40]
        cbw
        mov     dx, ax
        mov     al, [bx+40]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+40]
        cbw
        sub     dx, ax
        mov     ax, es:[di-176]
        add     ax, dx
        mov     es:[di+80], ax
        mov     al, [si+41]
        cbw
        mov     dx, ax
        mov     al, [bx+41]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+41]
        cbw
        sub     dx, ax
        mov     ax, es:[di-174]
        add     ax, dx
        mov     es:[di+82], ax
        mov     al, [si+42]
        cbw
        mov     dx, ax
        mov     al, [bx+42]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+42]
        cbw
        sub     dx, ax
        mov     ax, es:[di-172]
        add     ax, dx
        mov     es:[di+84], ax
        mov     al, [si+43]
        cbw
        mov     dx, ax
        mov     al, [bx+43]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+43]
        cbw
        sub     dx, ax
        mov     ax, es:[di-170]
        add     ax, dx
        mov     es:[di+86], ax
        mov     al, [si+44]
        cbw
        mov     dx, ax
        mov     al, [bx+44]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+44]
        cbw
        sub     dx, ax
        mov     ax, es:[di-168]
        add     ax, dx
        mov     es:[di+88], ax
        mov     al, [si+45]
        cbw
        mov     dx, ax
        mov     al, [bx+45]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+45]
        cbw
        sub     dx, ax
        mov     ax, es:[di-166]
        add     ax, dx
        mov     es:[di+90], ax
        mov     al, [si+46]
        cbw
        mov     dx, ax
        mov     al, [bx+46]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+46]
        cbw
        sub     dx, ax
        mov     ax, es:[di-164]
        add     ax, dx
        mov     es:[di+92], ax
        mov     al, [si+47]
        cbw
        mov     dx, ax
        mov     al, [bx+47]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+47]
        cbw
        sub     dx, ax
        mov     ax, es:[di-162]
        add     ax, dx
        mov     es:[di+94], ax
        mov     al, [si+48]
        cbw
        mov     dx, ax
        mov     al, [bx+48]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+48]
        cbw
        sub     dx, ax
        mov     ax, es:[di-160]
        add     ax, dx
        mov     es:[di+96], ax
        mov     al, [si+49]
        cbw
        mov     dx, ax
        mov     al, [bx+49]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+49]
        cbw
        sub     dx, ax
        mov     ax, es:[di-158]
        add     ax, dx
        mov     es:[di+98], ax
        mov     al, [si+50]
        cbw
        mov     dx, ax
        mov     al, [bx+50]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+50]
        cbw
        sub     dx, ax
        mov     ax, es:[di-156]
        add     ax, dx
        mov     es:[di+100], ax
        mov     al, [si+51]
        cbw
        mov     dx, ax
        mov     al, [bx+51]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+51]
        cbw
        sub     dx, ax
        mov     ax, es:[di-154]
        add     ax, dx
        mov     es:[di+102], ax
        mov     al, [si+52]
        cbw
        mov     dx, ax
        mov     al, [bx+52]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+52]
        cbw
        sub     dx, ax
        mov     ax, es:[di-152]
        add     ax, dx
        mov     es:[di+104], ax
        mov     al, [si+53]
        cbw
        mov     dx, ax
        mov     al, [bx+53]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+53]
        cbw
        sub     dx, ax
        mov     ax, es:[di-150]
        add     ax, dx
        mov     es:[di+106], ax
        mov     al, [si+54]
        cbw
        mov     dx, ax
        mov     al, [bx+54]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+54]
        cbw
        sub     dx, ax
        mov     ax, es:[di-148]
        add     ax, dx
        mov     es:[di+108], ax
        mov     al, [si+55]
        cbw
        mov     dx, ax
        mov     al, [bx+55]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+55]
        cbw
        sub     dx, ax
        mov     ax, es:[di-146]
        add     ax, dx
        mov     es:[di+110], ax
        mov     al, [si+56]
        cbw
        mov     dx, ax
        mov     al, [bx+56]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+56]
        cbw
        sub     dx, ax
        mov     ax, es:[di-144]
        add     ax, dx
        mov     es:[di+112], ax
        mov     al, [si+57]
        cbw
        mov     dx, ax
        mov     al, [bx+57]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+57]
        cbw
        sub     dx, ax
        mov     ax, es:[di-142]
        add     ax, dx
        mov     es:[di+114], ax
        mov     al, [si+58]
        cbw
        mov     dx, ax
        mov     al, [bx+58]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+58]
        cbw
        sub     dx, ax
        mov     ax, es:[di-140]
        add     ax, dx
        mov     es:[di+116], ax
        mov     al, [si+59]
        cbw
        mov     dx, ax
        mov     al, [bx+59]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+59]
        cbw
        sub     dx, ax
        mov     ax, es:[di-138]
        add     ax, dx
        mov     es:[di+118], ax
        mov     al, [si+60]
        cbw
        mov     dx, ax
        mov     al, [bx+60]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+60]
        cbw
        sub     dx, ax
        mov     ax, es:[di-136]
        add     ax, dx
        mov     es:[di+120], ax
        mov     al, [si+61]
        cbw
        mov     dx, ax
        mov     al, [bx+61]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+61]
        cbw
        sub     dx, ax
        mov     ax, es:[di-134]
        add     ax, dx
        mov     es:[di+122], ax
        mov     al, [si+62]
        cbw
        mov     dx, ax
        mov     al, [bx+62]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+62]
        cbw
        sub     dx, ax
        mov     ax, es:[di-132]
        add     ax, dx
        mov     es:[di+124], ax
        mov     al, [si+63]
        cbw
        mov     dx, ax
        mov     al, [bx+63]
        cbw
        sub     dx, ax
        mov     al, ds:[bp+63]
        cbw
        sub     dx, ax
        mov     ax, es:[di-130]
        add     ax, dx
        mov     es:[di+126], ax
        pop     ds
        pop     di
        pop     si
        pop     bp
        pop     bx
        retf
nn_make_cap_ ENDP

; =====================================================================
; nn_fwd_eval_ - NNUE forward pass via per-slot product tables.
; int nn_fwd_eval(int side);  ax = side (0 white, 1 black); returns eval in ax.
; stm/nstm handling (see NNUE.md): the net is SIDE-TO-MOVE-aware - acc[0] is the
; white POV, acc[1] the black POV, and the side to move is "white" in feature
; space. So BX points at the STM accumulator (_nn_acc + side*128) and BP at the
; NSTM one (_nn_acc + (1-side)*128); w2[0..63] is the STM weight row, w2[64..127]
; the NSTM row, and _nn_fwd holds those products ([2][64][256] i16; entry
; [p][j][a+128] = w2[p*64+j] * a, one 64 KB far segment: [0] at offset 0 and
; [1] at +32768, so the NSTM loads carry +32768. Fast path: |a|<128 -> index=2*a,
; clamp extremes take the (rare) shift handlers w2[di]<<7 (di = slot, or 64+slot
; for the NSTM row). SI:CX is the i32 accumulator; result is (acc >> NNUE_SCALE_SHIFT).
; Preserves bx,si,di,bp,ds; clobbers ax,dx,cx,es.
nn_fwd_eval_ PROC FAR
        push    bp
        push    bx
        push    si
        push    di
        push    ds
        mov     bx, OFFSET _nn_acc
        mov     cx, ss:_nn_ply
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1                   ; cx = ply*256
        add     bx, cx                  ; bx = _nn_acc + ply*256 (active slot)
        mov     dx, ax                  ; dx = side
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1                   ; dx = side*128
        mov     bp, bx
        add     bp, 128                 ; bp = _nn_acc + 128
        sub     bp, dx                  ; bp = _nn_acc + (1-side)*128 (NSTM)
        add     bx, dx                  ; bx = _nn_acc + side*128 (STM)
        mov     ax, SEG _nn_fwd
        mov     ds, ax                  ; ds = shared fwd0/fwd1 segment
        mov     ax, ss:_nn_bias
        cwd
        mov     si, ax                  ; si = bias low
        mov     cx, dx                  ; cx = bias high (sign-extended)
; ---- slot 0 ----
        mov     ax, ss:[bx+0]
        cmp     ax, 0x0080
        jge     P0S_0
        cmp     ax, 0xFF80
        jle     N0S_0
        add     ax, ax
        mov     di, ax
        mov     ax, [di+256]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_0
P0S_0:
        mov     di, 0
        call    FWD_SHIFT_ADD
        jmp     P1E_0
N0S_0:
        mov     di, 0
        call    FWD_SHIFT_SUB
P1E_0:
        mov     ax, ss:[bp+0]
        cmp     ax, 0x0080
        jge     P1S_0
        cmp     ax, 0xFF80
        jle     N1S_0
        add     ax, ax
        mov     di, ax
        mov     ax, [di+33024]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_0
P1S_0:
        mov     di, 64
        call    FWD_SHIFT_ADD
        jmp     NXT_0
N1S_0:
        mov     di, 64
        call    FWD_SHIFT_SUB
NXT_0:
; ---- slot 1 ----
        mov     ax, ss:[bx+2]
        cmp     ax, 0x0080
        jge     P0S_1
        cmp     ax, 0xFF80
        jle     N0S_1
        add     ax, ax
        mov     di, ax
        mov     ax, [di+768]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_1
P0S_1:
        mov     di, 1
        call    FWD_SHIFT_ADD
        jmp     P1E_1
N0S_1:
        mov     di, 1
        call    FWD_SHIFT_SUB
P1E_1:
        mov     ax, ss:[bp+2]
        cmp     ax, 0x0080
        jge     P1S_1
        cmp     ax, 0xFF80
        jle     N1S_1
        add     ax, ax
        mov     di, ax
        mov     ax, [di+33536]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_1
P1S_1:
        mov     di, 65
        call    FWD_SHIFT_ADD
        jmp     NXT_1
N1S_1:
        mov     di, 65
        call    FWD_SHIFT_SUB
NXT_1:
; ---- slot 2 ----
        mov     ax, ss:[bx+4]
        cmp     ax, 0x0080
        jge     P0S_2
        cmp     ax, 0xFF80
        jle     N0S_2
        add     ax, ax
        mov     di, ax
        mov     ax, [di+1280]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_2
P0S_2:
        mov     di, 2
        call    FWD_SHIFT_ADD
        jmp     P1E_2
N0S_2:
        mov     di, 2
        call    FWD_SHIFT_SUB
P1E_2:
        mov     ax, ss:[bp+4]
        cmp     ax, 0x0080
        jge     P1S_2
        cmp     ax, 0xFF80
        jle     N1S_2
        add     ax, ax
        mov     di, ax
        mov     ax, [di+34048]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_2
P1S_2:
        mov     di, 66
        call    FWD_SHIFT_ADD
        jmp     NXT_2
N1S_2:
        mov     di, 66
        call    FWD_SHIFT_SUB
NXT_2:
; ---- slot 3 ----
        mov     ax, ss:[bx+6]
        cmp     ax, 0x0080
        jge     P0S_3
        cmp     ax, 0xFF80
        jle     N0S_3
        add     ax, ax
        mov     di, ax
        mov     ax, [di+1792]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_3
P0S_3:
        mov     di, 3
        call    FWD_SHIFT_ADD
        jmp     P1E_3
N0S_3:
        mov     di, 3
        call    FWD_SHIFT_SUB
P1E_3:
        mov     ax, ss:[bp+6]
        cmp     ax, 0x0080
        jge     P1S_3
        cmp     ax, 0xFF80
        jle     N1S_3
        add     ax, ax
        mov     di, ax
        mov     ax, [di+34560]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_3
P1S_3:
        mov     di, 67
        call    FWD_SHIFT_ADD
        jmp     NXT_3
N1S_3:
        mov     di, 67
        call    FWD_SHIFT_SUB
NXT_3:
; ---- slot 4 ----
        mov     ax, ss:[bx+8]
        cmp     ax, 0x0080
        jge     P0S_4
        cmp     ax, 0xFF80
        jle     N0S_4
        add     ax, ax
        mov     di, ax
        mov     ax, [di+2304]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_4
P0S_4:
        mov     di, 4
        call    FWD_SHIFT_ADD
        jmp     P1E_4
N0S_4:
        mov     di, 4
        call    FWD_SHIFT_SUB
P1E_4:
        mov     ax, ss:[bp+8]
        cmp     ax, 0x0080
        jge     P1S_4
        cmp     ax, 0xFF80
        jle     N1S_4
        add     ax, ax
        mov     di, ax
        mov     ax, [di+35072]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_4
P1S_4:
        mov     di, 68
        call    FWD_SHIFT_ADD
        jmp     NXT_4
N1S_4:
        mov     di, 68
        call    FWD_SHIFT_SUB
NXT_4:
; ---- slot 5 ----
        mov     ax, ss:[bx+10]
        cmp     ax, 0x0080
        jge     P0S_5
        cmp     ax, 0xFF80
        jle     N0S_5
        add     ax, ax
        mov     di, ax
        mov     ax, [di+2816]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_5
P0S_5:
        mov     di, 5
        call    FWD_SHIFT_ADD
        jmp     P1E_5
N0S_5:
        mov     di, 5
        call    FWD_SHIFT_SUB
P1E_5:
        mov     ax, ss:[bp+10]
        cmp     ax, 0x0080
        jge     P1S_5
        cmp     ax, 0xFF80
        jle     N1S_5
        add     ax, ax
        mov     di, ax
        mov     ax, [di+35584]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_5
P1S_5:
        mov     di, 69
        call    FWD_SHIFT_ADD
        jmp     NXT_5
N1S_5:
        mov     di, 69
        call    FWD_SHIFT_SUB
NXT_5:
; ---- slot 6 ----
        mov     ax, ss:[bx+12]
        cmp     ax, 0x0080
        jge     P0S_6
        cmp     ax, 0xFF80
        jle     N0S_6
        add     ax, ax
        mov     di, ax
        mov     ax, [di+3328]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_6
P0S_6:
        mov     di, 6
        call    FWD_SHIFT_ADD
        jmp     P1E_6
N0S_6:
        mov     di, 6
        call    FWD_SHIFT_SUB
P1E_6:
        mov     ax, ss:[bp+12]
        cmp     ax, 0x0080
        jge     P1S_6
        cmp     ax, 0xFF80
        jle     N1S_6
        add     ax, ax
        mov     di, ax
        mov     ax, [di+36096]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_6
P1S_6:
        mov     di, 70
        call    FWD_SHIFT_ADD
        jmp     NXT_6
N1S_6:
        mov     di, 70
        call    FWD_SHIFT_SUB
NXT_6:
; ---- slot 7 ----
        mov     ax, ss:[bx+14]
        cmp     ax, 0x0080
        jge     P0S_7
        cmp     ax, 0xFF80
        jle     N0S_7
        add     ax, ax
        mov     di, ax
        mov     ax, [di+3840]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_7
P0S_7:
        mov     di, 7
        call    FWD_SHIFT_ADD
        jmp     P1E_7
N0S_7:
        mov     di, 7
        call    FWD_SHIFT_SUB
P1E_7:
        mov     ax, ss:[bp+14]
        cmp     ax, 0x0080
        jge     P1S_7
        cmp     ax, 0xFF80
        jle     N1S_7
        add     ax, ax
        mov     di, ax
        mov     ax, [di+36608]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_7
P1S_7:
        mov     di, 71
        call    FWD_SHIFT_ADD
        jmp     NXT_7
N1S_7:
        mov     di, 71
        call    FWD_SHIFT_SUB
NXT_7:
; ---- slot 8 ----
        mov     ax, ss:[bx+16]
        cmp     ax, 0x0080
        jge     P0S_8
        cmp     ax, 0xFF80
        jle     N0S_8
        add     ax, ax
        mov     di, ax
        mov     ax, [di+4352]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_8
P0S_8:
        mov     di, 8
        call    FWD_SHIFT_ADD
        jmp     P1E_8
N0S_8:
        mov     di, 8
        call    FWD_SHIFT_SUB
P1E_8:
        mov     ax, ss:[bp+16]
        cmp     ax, 0x0080
        jge     P1S_8
        cmp     ax, 0xFF80
        jle     N1S_8
        add     ax, ax
        mov     di, ax
        mov     ax, [di+37120]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_8
P1S_8:
        mov     di, 72
        call    FWD_SHIFT_ADD
        jmp     NXT_8
N1S_8:
        mov     di, 72
        call    FWD_SHIFT_SUB
NXT_8:
; ---- slot 9 ----
        mov     ax, ss:[bx+18]
        cmp     ax, 0x0080
        jge     P0S_9
        cmp     ax, 0xFF80
        jle     N0S_9
        add     ax, ax
        mov     di, ax
        mov     ax, [di+4864]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_9
P0S_9:
        mov     di, 9
        call    FWD_SHIFT_ADD
        jmp     P1E_9
N0S_9:
        mov     di, 9
        call    FWD_SHIFT_SUB
P1E_9:
        mov     ax, ss:[bp+18]
        cmp     ax, 0x0080
        jge     P1S_9
        cmp     ax, 0xFF80
        jle     N1S_9
        add     ax, ax
        mov     di, ax
        mov     ax, [di+37632]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_9
P1S_9:
        mov     di, 73
        call    FWD_SHIFT_ADD
        jmp     NXT_9
N1S_9:
        mov     di, 73
        call    FWD_SHIFT_SUB
NXT_9:
; ---- slot 10 ----
        mov     ax, ss:[bx+20]
        cmp     ax, 0x0080
        jge     P0S_10
        cmp     ax, 0xFF80
        jle     N0S_10
        add     ax, ax
        mov     di, ax
        mov     ax, [di+5376]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_10
P0S_10:
        mov     di, 10
        call    FWD_SHIFT_ADD
        jmp     P1E_10
N0S_10:
        mov     di, 10
        call    FWD_SHIFT_SUB
P1E_10:
        mov     ax, ss:[bp+20]
        cmp     ax, 0x0080
        jge     P1S_10
        cmp     ax, 0xFF80
        jle     N1S_10
        add     ax, ax
        mov     di, ax
        mov     ax, [di+38144]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_10
P1S_10:
        mov     di, 74
        call    FWD_SHIFT_ADD
        jmp     NXT_10
N1S_10:
        mov     di, 74
        call    FWD_SHIFT_SUB
NXT_10:
; ---- slot 11 ----
        mov     ax, ss:[bx+22]
        cmp     ax, 0x0080
        jge     P0S_11
        cmp     ax, 0xFF80
        jle     N0S_11
        add     ax, ax
        mov     di, ax
        mov     ax, [di+5888]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_11
P0S_11:
        mov     di, 11
        call    FWD_SHIFT_ADD
        jmp     P1E_11
N0S_11:
        mov     di, 11
        call    FWD_SHIFT_SUB
P1E_11:
        mov     ax, ss:[bp+22]
        cmp     ax, 0x0080
        jge     P1S_11
        cmp     ax, 0xFF80
        jle     N1S_11
        add     ax, ax
        mov     di, ax
        mov     ax, [di+38656]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_11
P1S_11:
        mov     di, 75
        call    FWD_SHIFT_ADD
        jmp     NXT_11
N1S_11:
        mov     di, 75
        call    FWD_SHIFT_SUB
NXT_11:
; ---- slot 12 ----
        mov     ax, ss:[bx+24]
        cmp     ax, 0x0080
        jge     P0S_12
        cmp     ax, 0xFF80
        jle     N0S_12
        add     ax, ax
        mov     di, ax
        mov     ax, [di+6400]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_12
P0S_12:
        mov     di, 12
        call    FWD_SHIFT_ADD
        jmp     P1E_12
N0S_12:
        mov     di, 12
        call    FWD_SHIFT_SUB
P1E_12:
        mov     ax, ss:[bp+24]
        cmp     ax, 0x0080
        jge     P1S_12
        cmp     ax, 0xFF80
        jle     N1S_12
        add     ax, ax
        mov     di, ax
        mov     ax, [di+39168]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_12
P1S_12:
        mov     di, 76
        call    FWD_SHIFT_ADD
        jmp     NXT_12
N1S_12:
        mov     di, 76
        call    FWD_SHIFT_SUB
NXT_12:
; ---- slot 13 ----
        mov     ax, ss:[bx+26]
        cmp     ax, 0x0080
        jge     P0S_13
        cmp     ax, 0xFF80
        jle     N0S_13
        add     ax, ax
        mov     di, ax
        mov     ax, [di+6912]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_13
P0S_13:
        mov     di, 13
        call    FWD_SHIFT_ADD
        jmp     P1E_13
N0S_13:
        mov     di, 13
        call    FWD_SHIFT_SUB
P1E_13:
        mov     ax, ss:[bp+26]
        cmp     ax, 0x0080
        jge     P1S_13
        cmp     ax, 0xFF80
        jle     N1S_13
        add     ax, ax
        mov     di, ax
        mov     ax, [di+39680]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_13
P1S_13:
        mov     di, 77
        call    FWD_SHIFT_ADD
        jmp     NXT_13
N1S_13:
        mov     di, 77
        call    FWD_SHIFT_SUB
NXT_13:
; ---- slot 14 ----
        mov     ax, ss:[bx+28]
        cmp     ax, 0x0080
        jge     P0S_14
        cmp     ax, 0xFF80
        jle     N0S_14
        add     ax, ax
        mov     di, ax
        mov     ax, [di+7424]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_14
P0S_14:
        mov     di, 14
        call    FWD_SHIFT_ADD
        jmp     P1E_14
N0S_14:
        mov     di, 14
        call    FWD_SHIFT_SUB
P1E_14:
        mov     ax, ss:[bp+28]
        cmp     ax, 0x0080
        jge     P1S_14
        cmp     ax, 0xFF80
        jle     N1S_14
        add     ax, ax
        mov     di, ax
        mov     ax, [di+40192]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_14
P1S_14:
        mov     di, 78
        call    FWD_SHIFT_ADD
        jmp     NXT_14
N1S_14:
        mov     di, 78
        call    FWD_SHIFT_SUB
NXT_14:
; ---- slot 15 ----
        mov     ax, ss:[bx+30]
        cmp     ax, 0x0080
        jge     P0S_15
        cmp     ax, 0xFF80
        jle     N0S_15
        add     ax, ax
        mov     di, ax
        mov     ax, [di+7936]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_15
P0S_15:
        mov     di, 15
        call    FWD_SHIFT_ADD
        jmp     P1E_15
N0S_15:
        mov     di, 15
        call    FWD_SHIFT_SUB
P1E_15:
        mov     ax, ss:[bp+30]
        cmp     ax, 0x0080
        jge     P1S_15
        cmp     ax, 0xFF80
        jle     N1S_15
        add     ax, ax
        mov     di, ax
        mov     ax, [di+40704]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_15
P1S_15:
        mov     di, 79
        call    FWD_SHIFT_ADD
        jmp     NXT_15
N1S_15:
        mov     di, 79
        call    FWD_SHIFT_SUB
NXT_15:
; ---- slot 16 ----
        mov     ax, ss:[bx+32]
        cmp     ax, 0x0080
        jge     P0S_16
        cmp     ax, 0xFF80
        jle     N0S_16
        add     ax, ax
        mov     di, ax
        mov     ax, [di+8448]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_16
P0S_16:
        mov     di, 16
        call    FWD_SHIFT_ADD
        jmp     P1E_16
N0S_16:
        mov     di, 16
        call    FWD_SHIFT_SUB
P1E_16:
        mov     ax, ss:[bp+32]
        cmp     ax, 0x0080
        jge     P1S_16
        cmp     ax, 0xFF80
        jle     N1S_16
        add     ax, ax
        mov     di, ax
        mov     ax, [di+41216]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_16
P1S_16:
        mov     di, 80
        call    FWD_SHIFT_ADD
        jmp     NXT_16
N1S_16:
        mov     di, 80
        call    FWD_SHIFT_SUB
NXT_16:
; ---- slot 17 ----
        mov     ax, ss:[bx+34]
        cmp     ax, 0x0080
        jge     P0S_17
        cmp     ax, 0xFF80
        jle     N0S_17
        add     ax, ax
        mov     di, ax
        mov     ax, [di+8960]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_17
P0S_17:
        mov     di, 17
        call    FWD_SHIFT_ADD
        jmp     P1E_17
N0S_17:
        mov     di, 17
        call    FWD_SHIFT_SUB
P1E_17:
        mov     ax, ss:[bp+34]
        cmp     ax, 0x0080
        jge     P1S_17
        cmp     ax, 0xFF80
        jle     N1S_17
        add     ax, ax
        mov     di, ax
        mov     ax, [di+41728]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_17
P1S_17:
        mov     di, 81
        call    FWD_SHIFT_ADD
        jmp     NXT_17
N1S_17:
        mov     di, 81
        call    FWD_SHIFT_SUB
NXT_17:
; ---- slot 18 ----
        mov     ax, ss:[bx+36]
        cmp     ax, 0x0080
        jge     P0S_18
        cmp     ax, 0xFF80
        jle     N0S_18
        add     ax, ax
        mov     di, ax
        mov     ax, [di+9472]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_18
P0S_18:
        mov     di, 18
        call    FWD_SHIFT_ADD
        jmp     P1E_18
N0S_18:
        mov     di, 18
        call    FWD_SHIFT_SUB
P1E_18:
        mov     ax, ss:[bp+36]
        cmp     ax, 0x0080
        jge     P1S_18
        cmp     ax, 0xFF80
        jle     N1S_18
        add     ax, ax
        mov     di, ax
        mov     ax, [di+42240]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_18
P1S_18:
        mov     di, 82
        call    FWD_SHIFT_ADD
        jmp     NXT_18
N1S_18:
        mov     di, 82
        call    FWD_SHIFT_SUB
NXT_18:
; ---- slot 19 ----
        mov     ax, ss:[bx+38]
        cmp     ax, 0x0080
        jge     P0S_19
        cmp     ax, 0xFF80
        jle     N0S_19
        add     ax, ax
        mov     di, ax
        mov     ax, [di+9984]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_19
P0S_19:
        mov     di, 19
        call    FWD_SHIFT_ADD
        jmp     P1E_19
N0S_19:
        mov     di, 19
        call    FWD_SHIFT_SUB
P1E_19:
        mov     ax, ss:[bp+38]
        cmp     ax, 0x0080
        jge     P1S_19
        cmp     ax, 0xFF80
        jle     N1S_19
        add     ax, ax
        mov     di, ax
        mov     ax, [di+42752]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_19
P1S_19:
        mov     di, 83
        call    FWD_SHIFT_ADD
        jmp     NXT_19
N1S_19:
        mov     di, 83
        call    FWD_SHIFT_SUB
NXT_19:
; ---- slot 20 ----
        mov     ax, ss:[bx+40]
        cmp     ax, 0x0080
        jge     P0S_20
        cmp     ax, 0xFF80
        jle     N0S_20
        add     ax, ax
        mov     di, ax
        mov     ax, [di+10496]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_20
P0S_20:
        mov     di, 20
        call    FWD_SHIFT_ADD
        jmp     P1E_20
N0S_20:
        mov     di, 20
        call    FWD_SHIFT_SUB
P1E_20:
        mov     ax, ss:[bp+40]
        cmp     ax, 0x0080
        jge     P1S_20
        cmp     ax, 0xFF80
        jle     N1S_20
        add     ax, ax
        mov     di, ax
        mov     ax, [di+43264]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_20
P1S_20:
        mov     di, 84
        call    FWD_SHIFT_ADD
        jmp     NXT_20
N1S_20:
        mov     di, 84
        call    FWD_SHIFT_SUB
NXT_20:
; ---- slot 21 ----
        mov     ax, ss:[bx+42]
        cmp     ax, 0x0080
        jge     P0S_21
        cmp     ax, 0xFF80
        jle     N0S_21
        add     ax, ax
        mov     di, ax
        mov     ax, [di+11008]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_21
P0S_21:
        mov     di, 21
        call    FWD_SHIFT_ADD
        jmp     P1E_21
N0S_21:
        mov     di, 21
        call    FWD_SHIFT_SUB
P1E_21:
        mov     ax, ss:[bp+42]
        cmp     ax, 0x0080
        jge     P1S_21
        cmp     ax, 0xFF80
        jle     N1S_21
        add     ax, ax
        mov     di, ax
        mov     ax, [di+43776]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_21
P1S_21:
        mov     di, 85
        call    FWD_SHIFT_ADD
        jmp     NXT_21
N1S_21:
        mov     di, 85
        call    FWD_SHIFT_SUB
NXT_21:
; ---- slot 22 ----
        mov     ax, ss:[bx+44]
        cmp     ax, 0x0080
        jge     P0S_22
        cmp     ax, 0xFF80
        jle     N0S_22
        add     ax, ax
        mov     di, ax
        mov     ax, [di+11520]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_22
P0S_22:
        mov     di, 22
        call    FWD_SHIFT_ADD
        jmp     P1E_22
N0S_22:
        mov     di, 22
        call    FWD_SHIFT_SUB
P1E_22:
        mov     ax, ss:[bp+44]
        cmp     ax, 0x0080
        jge     P1S_22
        cmp     ax, 0xFF80
        jle     N1S_22
        add     ax, ax
        mov     di, ax
        mov     ax, [di+44288]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_22
P1S_22:
        mov     di, 86
        call    FWD_SHIFT_ADD
        jmp     NXT_22
N1S_22:
        mov     di, 86
        call    FWD_SHIFT_SUB
NXT_22:
; ---- slot 23 ----
        mov     ax, ss:[bx+46]
        cmp     ax, 0x0080
        jge     P0S_23
        cmp     ax, 0xFF80
        jle     N0S_23
        add     ax, ax
        mov     di, ax
        mov     ax, [di+12032]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_23
P0S_23:
        mov     di, 23
        call    FWD_SHIFT_ADD
        jmp     P1E_23
N0S_23:
        mov     di, 23
        call    FWD_SHIFT_SUB
P1E_23:
        mov     ax, ss:[bp+46]
        cmp     ax, 0x0080
        jge     P1S_23
        cmp     ax, 0xFF80
        jle     N1S_23
        add     ax, ax
        mov     di, ax
        mov     ax, [di+44800]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_23
P1S_23:
        mov     di, 87
        call    FWD_SHIFT_ADD
        jmp     NXT_23
N1S_23:
        mov     di, 87
        call    FWD_SHIFT_SUB
NXT_23:
; ---- slot 24 ----
        mov     ax, ss:[bx+48]
        cmp     ax, 0x0080
        jge     P0S_24
        cmp     ax, 0xFF80
        jle     N0S_24
        add     ax, ax
        mov     di, ax
        mov     ax, [di+12544]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_24
P0S_24:
        mov     di, 24
        call    FWD_SHIFT_ADD
        jmp     P1E_24
N0S_24:
        mov     di, 24
        call    FWD_SHIFT_SUB
P1E_24:
        mov     ax, ss:[bp+48]
        cmp     ax, 0x0080
        jge     P1S_24
        cmp     ax, 0xFF80
        jle     N1S_24
        add     ax, ax
        mov     di, ax
        mov     ax, [di+45312]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_24
P1S_24:
        mov     di, 88
        call    FWD_SHIFT_ADD
        jmp     NXT_24
N1S_24:
        mov     di, 88
        call    FWD_SHIFT_SUB
NXT_24:
; ---- slot 25 ----
        mov     ax, ss:[bx+50]
        cmp     ax, 0x0080
        jge     P0S_25
        cmp     ax, 0xFF80
        jle     N0S_25
        add     ax, ax
        mov     di, ax
        mov     ax, [di+13056]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_25
P0S_25:
        mov     di, 25
        call    FWD_SHIFT_ADD
        jmp     P1E_25
N0S_25:
        mov     di, 25
        call    FWD_SHIFT_SUB
P1E_25:
        mov     ax, ss:[bp+50]
        cmp     ax, 0x0080
        jge     P1S_25
        cmp     ax, 0xFF80
        jle     N1S_25
        add     ax, ax
        mov     di, ax
        mov     ax, [di+45824]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_25
P1S_25:
        mov     di, 89
        call    FWD_SHIFT_ADD
        jmp     NXT_25
N1S_25:
        mov     di, 89
        call    FWD_SHIFT_SUB
NXT_25:
; ---- slot 26 ----
        mov     ax, ss:[bx+52]
        cmp     ax, 0x0080
        jge     P0S_26
        cmp     ax, 0xFF80
        jle     N0S_26
        add     ax, ax
        mov     di, ax
        mov     ax, [di+13568]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_26
P0S_26:
        mov     di, 26
        call    FWD_SHIFT_ADD
        jmp     P1E_26
N0S_26:
        mov     di, 26
        call    FWD_SHIFT_SUB
P1E_26:
        mov     ax, ss:[bp+52]
        cmp     ax, 0x0080
        jge     P1S_26
        cmp     ax, 0xFF80
        jle     N1S_26
        add     ax, ax
        mov     di, ax
        mov     ax, [di+46336]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_26
P1S_26:
        mov     di, 90
        call    FWD_SHIFT_ADD
        jmp     NXT_26
N1S_26:
        mov     di, 90
        call    FWD_SHIFT_SUB
NXT_26:
; ---- slot 27 ----
        mov     ax, ss:[bx+54]
        cmp     ax, 0x0080
        jge     P0S_27
        cmp     ax, 0xFF80
        jle     N0S_27
        add     ax, ax
        mov     di, ax
        mov     ax, [di+14080]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_27
P0S_27:
        mov     di, 27
        call    FWD_SHIFT_ADD
        jmp     P1E_27
N0S_27:
        mov     di, 27
        call    FWD_SHIFT_SUB
P1E_27:
        mov     ax, ss:[bp+54]
        cmp     ax, 0x0080
        jge     P1S_27
        cmp     ax, 0xFF80
        jle     N1S_27
        add     ax, ax
        mov     di, ax
        mov     ax, [di+46848]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_27
P1S_27:
        mov     di, 91
        call    FWD_SHIFT_ADD
        jmp     NXT_27
N1S_27:
        mov     di, 91
        call    FWD_SHIFT_SUB
NXT_27:
; ---- slot 28 ----
        mov     ax, ss:[bx+56]
        cmp     ax, 0x0080
        jge     P0S_28
        cmp     ax, 0xFF80
        jle     N0S_28
        add     ax, ax
        mov     di, ax
        mov     ax, [di+14592]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_28
P0S_28:
        mov     di, 28
        call    FWD_SHIFT_ADD
        jmp     P1E_28
N0S_28:
        mov     di, 28
        call    FWD_SHIFT_SUB
P1E_28:
        mov     ax, ss:[bp+56]
        cmp     ax, 0x0080
        jge     P1S_28
        cmp     ax, 0xFF80
        jle     N1S_28
        add     ax, ax
        mov     di, ax
        mov     ax, [di+47360]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_28
P1S_28:
        mov     di, 92
        call    FWD_SHIFT_ADD
        jmp     NXT_28
N1S_28:
        mov     di, 92
        call    FWD_SHIFT_SUB
NXT_28:
; ---- slot 29 ----
        mov     ax, ss:[bx+58]
        cmp     ax, 0x0080
        jge     P0S_29
        cmp     ax, 0xFF80
        jle     N0S_29
        add     ax, ax
        mov     di, ax
        mov     ax, [di+15104]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_29
P0S_29:
        mov     di, 29
        call    FWD_SHIFT_ADD
        jmp     P1E_29
N0S_29:
        mov     di, 29
        call    FWD_SHIFT_SUB
P1E_29:
        mov     ax, ss:[bp+58]
        cmp     ax, 0x0080
        jge     P1S_29
        cmp     ax, 0xFF80
        jle     N1S_29
        add     ax, ax
        mov     di, ax
        mov     ax, [di+47872]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_29
P1S_29:
        mov     di, 93
        call    FWD_SHIFT_ADD
        jmp     NXT_29
N1S_29:
        mov     di, 93
        call    FWD_SHIFT_SUB
NXT_29:
; ---- slot 30 ----
        mov     ax, ss:[bx+60]
        cmp     ax, 0x0080
        jge     P0S_30
        cmp     ax, 0xFF80
        jle     N0S_30
        add     ax, ax
        mov     di, ax
        mov     ax, [di+15616]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_30
P0S_30:
        mov     di, 30
        call    FWD_SHIFT_ADD
        jmp     P1E_30
N0S_30:
        mov     di, 30
        call    FWD_SHIFT_SUB
P1E_30:
        mov     ax, ss:[bp+60]
        cmp     ax, 0x0080
        jge     P1S_30
        cmp     ax, 0xFF80
        jle     N1S_30
        add     ax, ax
        mov     di, ax
        mov     ax, [di+48384]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_30
P1S_30:
        mov     di, 94
        call    FWD_SHIFT_ADD
        jmp     NXT_30
N1S_30:
        mov     di, 94
        call    FWD_SHIFT_SUB
NXT_30:
; ---- slot 31 ----
        mov     ax, ss:[bx+62]
        cmp     ax, 0x0080
        jge     P0S_31
        cmp     ax, 0xFF80
        jle     N0S_31
        add     ax, ax
        mov     di, ax
        mov     ax, [di+16128]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_31
P0S_31:
        mov     di, 31
        call    FWD_SHIFT_ADD
        jmp     P1E_31
N0S_31:
        mov     di, 31
        call    FWD_SHIFT_SUB
P1E_31:
        mov     ax, ss:[bp+62]
        cmp     ax, 0x0080
        jge     P1S_31
        cmp     ax, 0xFF80
        jle     N1S_31
        add     ax, ax
        mov     di, ax
        mov     ax, [di+48896]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_31
P1S_31:
        mov     di, 95
        call    FWD_SHIFT_ADD
        jmp     NXT_31
N1S_31:
        mov     di, 95
        call    FWD_SHIFT_SUB
NXT_31:
; ---- slot 32 ----
        mov     ax, ss:[bx+64]
        cmp     ax, 0x0080
        jge     P0S_32
        cmp     ax, 0xFF80
        jle     N0S_32
        add     ax, ax
        mov     di, ax
        mov     ax, [di+16640]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_32
P0S_32:
        mov     di, 32
        call    FWD_SHIFT_ADD
        jmp     P1E_32
N0S_32:
        mov     di, 32
        call    FWD_SHIFT_SUB
P1E_32:
        mov     ax, ss:[bp+64]
        cmp     ax, 0x0080
        jge     P1S_32
        cmp     ax, 0xFF80
        jle     N1S_32
        add     ax, ax
        mov     di, ax
        mov     ax, [di+49408]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_32
P1S_32:
        mov     di, 96
        call    FWD_SHIFT_ADD
        jmp     NXT_32
N1S_32:
        mov     di, 96
        call    FWD_SHIFT_SUB
NXT_32:
; ---- slot 33 ----
        mov     ax, ss:[bx+66]
        cmp     ax, 0x0080
        jge     P0S_33
        cmp     ax, 0xFF80
        jle     N0S_33
        add     ax, ax
        mov     di, ax
        mov     ax, [di+17152]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_33
P0S_33:
        mov     di, 33
        call    FWD_SHIFT_ADD
        jmp     P1E_33
N0S_33:
        mov     di, 33
        call    FWD_SHIFT_SUB
P1E_33:
        mov     ax, ss:[bp+66]
        cmp     ax, 0x0080
        jge     P1S_33
        cmp     ax, 0xFF80
        jle     N1S_33
        add     ax, ax
        mov     di, ax
        mov     ax, [di+49920]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_33
P1S_33:
        mov     di, 97
        call    FWD_SHIFT_ADD
        jmp     NXT_33
N1S_33:
        mov     di, 97
        call    FWD_SHIFT_SUB
NXT_33:
; ---- slot 34 ----
        mov     ax, ss:[bx+68]
        cmp     ax, 0x0080
        jge     P0S_34
        cmp     ax, 0xFF80
        jle     N0S_34
        add     ax, ax
        mov     di, ax
        mov     ax, [di+17664]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_34
P0S_34:
        mov     di, 34
        call    FWD_SHIFT_ADD
        jmp     P1E_34
N0S_34:
        mov     di, 34
        call    FWD_SHIFT_SUB
P1E_34:
        mov     ax, ss:[bp+68]
        cmp     ax, 0x0080
        jge     P1S_34
        cmp     ax, 0xFF80
        jle     N1S_34
        add     ax, ax
        mov     di, ax
        mov     ax, [di+50432]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_34
P1S_34:
        mov     di, 98
        call    FWD_SHIFT_ADD
        jmp     NXT_34
N1S_34:
        mov     di, 98
        call    FWD_SHIFT_SUB
NXT_34:
; ---- slot 35 ----
        mov     ax, ss:[bx+70]
        cmp     ax, 0x0080
        jge     P0S_35
        cmp     ax, 0xFF80
        jle     N0S_35
        add     ax, ax
        mov     di, ax
        mov     ax, [di+18176]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_35
P0S_35:
        mov     di, 35
        call    FWD_SHIFT_ADD
        jmp     P1E_35
N0S_35:
        mov     di, 35
        call    FWD_SHIFT_SUB
P1E_35:
        mov     ax, ss:[bp+70]
        cmp     ax, 0x0080
        jge     P1S_35
        cmp     ax, 0xFF80
        jle     N1S_35
        add     ax, ax
        mov     di, ax
        mov     ax, [di+50944]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_35
P1S_35:
        mov     di, 99
        call    FWD_SHIFT_ADD
        jmp     NXT_35
N1S_35:
        mov     di, 99
        call    FWD_SHIFT_SUB
NXT_35:
; ---- slot 36 ----
        mov     ax, ss:[bx+72]
        cmp     ax, 0x0080
        jge     P0S_36
        cmp     ax, 0xFF80
        jle     N0S_36
        add     ax, ax
        mov     di, ax
        mov     ax, [di+18688]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_36
P0S_36:
        mov     di, 36
        call    FWD_SHIFT_ADD
        jmp     P1E_36
N0S_36:
        mov     di, 36
        call    FWD_SHIFT_SUB
P1E_36:
        mov     ax, ss:[bp+72]
        cmp     ax, 0x0080
        jge     P1S_36
        cmp     ax, 0xFF80
        jle     N1S_36
        add     ax, ax
        mov     di, ax
        mov     ax, [di+51456]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_36
P1S_36:
        mov     di, 100
        call    FWD_SHIFT_ADD
        jmp     NXT_36
N1S_36:
        mov     di, 100
        call    FWD_SHIFT_SUB
NXT_36:
; ---- slot 37 ----
        mov     ax, ss:[bx+74]
        cmp     ax, 0x0080
        jge     P0S_37
        cmp     ax, 0xFF80
        jle     N0S_37
        add     ax, ax
        mov     di, ax
        mov     ax, [di+19200]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_37
P0S_37:
        mov     di, 37
        call    FWD_SHIFT_ADD
        jmp     P1E_37
N0S_37:
        mov     di, 37
        call    FWD_SHIFT_SUB
P1E_37:
        mov     ax, ss:[bp+74]
        cmp     ax, 0x0080
        jge     P1S_37
        cmp     ax, 0xFF80
        jle     N1S_37
        add     ax, ax
        mov     di, ax
        mov     ax, [di+51968]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_37
P1S_37:
        mov     di, 101
        call    FWD_SHIFT_ADD
        jmp     NXT_37
N1S_37:
        mov     di, 101
        call    FWD_SHIFT_SUB
NXT_37:
; ---- slot 38 ----
        mov     ax, ss:[bx+76]
        cmp     ax, 0x0080
        jge     P0S_38
        cmp     ax, 0xFF80
        jle     N0S_38
        add     ax, ax
        mov     di, ax
        mov     ax, [di+19712]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_38
P0S_38:
        mov     di, 38
        call    FWD_SHIFT_ADD
        jmp     P1E_38
N0S_38:
        mov     di, 38
        call    FWD_SHIFT_SUB
P1E_38:
        mov     ax, ss:[bp+76]
        cmp     ax, 0x0080
        jge     P1S_38
        cmp     ax, 0xFF80
        jle     N1S_38
        add     ax, ax
        mov     di, ax
        mov     ax, [di+52480]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_38
P1S_38:
        mov     di, 102
        call    FWD_SHIFT_ADD
        jmp     NXT_38
N1S_38:
        mov     di, 102
        call    FWD_SHIFT_SUB
NXT_38:
; ---- slot 39 ----
        mov     ax, ss:[bx+78]
        cmp     ax, 0x0080
        jge     P0S_39
        cmp     ax, 0xFF80
        jle     N0S_39
        add     ax, ax
        mov     di, ax
        mov     ax, [di+20224]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_39
P0S_39:
        mov     di, 39
        call    FWD_SHIFT_ADD
        jmp     P1E_39
N0S_39:
        mov     di, 39
        call    FWD_SHIFT_SUB
P1E_39:
        mov     ax, ss:[bp+78]
        cmp     ax, 0x0080
        jge     P1S_39
        cmp     ax, 0xFF80
        jle     N1S_39
        add     ax, ax
        mov     di, ax
        mov     ax, [di+52992]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_39
P1S_39:
        mov     di, 103
        call    FWD_SHIFT_ADD
        jmp     NXT_39
N1S_39:
        mov     di, 103
        call    FWD_SHIFT_SUB
NXT_39:
; ---- slot 40 ----
        mov     ax, ss:[bx+80]
        cmp     ax, 0x0080
        jge     P0S_40
        cmp     ax, 0xFF80
        jle     N0S_40
        add     ax, ax
        mov     di, ax
        mov     ax, [di+20736]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_40
P0S_40:
        mov     di, 40
        call    FWD_SHIFT_ADD
        jmp     P1E_40
N0S_40:
        mov     di, 40
        call    FWD_SHIFT_SUB
P1E_40:
        mov     ax, ss:[bp+80]
        cmp     ax, 0x0080
        jge     P1S_40
        cmp     ax, 0xFF80
        jle     N1S_40
        add     ax, ax
        mov     di, ax
        mov     ax, [di+53504]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_40
P1S_40:
        mov     di, 104
        call    FWD_SHIFT_ADD
        jmp     NXT_40
N1S_40:
        mov     di, 104
        call    FWD_SHIFT_SUB
NXT_40:
; ---- slot 41 ----
        mov     ax, ss:[bx+82]
        cmp     ax, 0x0080
        jge     P0S_41
        cmp     ax, 0xFF80
        jle     N0S_41
        add     ax, ax
        mov     di, ax
        mov     ax, [di+21248]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_41
P0S_41:
        mov     di, 41
        call    FWD_SHIFT_ADD
        jmp     P1E_41
N0S_41:
        mov     di, 41
        call    FWD_SHIFT_SUB
P1E_41:
        mov     ax, ss:[bp+82]
        cmp     ax, 0x0080
        jge     P1S_41
        cmp     ax, 0xFF80
        jle     N1S_41
        add     ax, ax
        mov     di, ax
        mov     ax, [di+54016]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_41
P1S_41:
        mov     di, 105
        call    FWD_SHIFT_ADD
        jmp     NXT_41
N1S_41:
        mov     di, 105
        call    FWD_SHIFT_SUB
NXT_41:
; ---- slot 42 ----
        mov     ax, ss:[bx+84]
        cmp     ax, 0x0080
        jge     P0S_42
        cmp     ax, 0xFF80
        jle     N0S_42
        add     ax, ax
        mov     di, ax
        mov     ax, [di+21760]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_42
P0S_42:
        mov     di, 42
        call    FWD_SHIFT_ADD
        jmp     P1E_42
N0S_42:
        mov     di, 42
        call    FWD_SHIFT_SUB
P1E_42:
        mov     ax, ss:[bp+84]
        cmp     ax, 0x0080
        jge     P1S_42
        cmp     ax, 0xFF80
        jle     N1S_42
        add     ax, ax
        mov     di, ax
        mov     ax, [di+54528]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_42
P1S_42:
        mov     di, 106
        call    FWD_SHIFT_ADD
        jmp     NXT_42
N1S_42:
        mov     di, 106
        call    FWD_SHIFT_SUB
NXT_42:
; ---- slot 43 ----
        mov     ax, ss:[bx+86]
        cmp     ax, 0x0080
        jge     P0S_43
        cmp     ax, 0xFF80
        jle     N0S_43
        add     ax, ax
        mov     di, ax
        mov     ax, [di+22272]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_43
P0S_43:
        mov     di, 43
        call    FWD_SHIFT_ADD
        jmp     P1E_43
N0S_43:
        mov     di, 43
        call    FWD_SHIFT_SUB
P1E_43:
        mov     ax, ss:[bp+86]
        cmp     ax, 0x0080
        jge     P1S_43
        cmp     ax, 0xFF80
        jle     N1S_43
        add     ax, ax
        mov     di, ax
        mov     ax, [di+55040]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_43
P1S_43:
        mov     di, 107
        call    FWD_SHIFT_ADD
        jmp     NXT_43
N1S_43:
        mov     di, 107
        call    FWD_SHIFT_SUB
NXT_43:
; ---- slot 44 ----
        mov     ax, ss:[bx+88]
        cmp     ax, 0x0080
        jge     P0S_44
        cmp     ax, 0xFF80
        jle     N0S_44
        add     ax, ax
        mov     di, ax
        mov     ax, [di+22784]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_44
P0S_44:
        mov     di, 44
        call    FWD_SHIFT_ADD
        jmp     P1E_44
N0S_44:
        mov     di, 44
        call    FWD_SHIFT_SUB
P1E_44:
        mov     ax, ss:[bp+88]
        cmp     ax, 0x0080
        jge     P1S_44
        cmp     ax, 0xFF80
        jle     N1S_44
        add     ax, ax
        mov     di, ax
        mov     ax, [di+55552]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_44
P1S_44:
        mov     di, 108
        call    FWD_SHIFT_ADD
        jmp     NXT_44
N1S_44:
        mov     di, 108
        call    FWD_SHIFT_SUB
NXT_44:
; ---- slot 45 ----
        mov     ax, ss:[bx+90]
        cmp     ax, 0x0080
        jge     P0S_45
        cmp     ax, 0xFF80
        jle     N0S_45
        add     ax, ax
        mov     di, ax
        mov     ax, [di+23296]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_45
P0S_45:
        mov     di, 45
        call    FWD_SHIFT_ADD
        jmp     P1E_45
N0S_45:
        mov     di, 45
        call    FWD_SHIFT_SUB
P1E_45:
        mov     ax, ss:[bp+90]
        cmp     ax, 0x0080
        jge     P1S_45
        cmp     ax, 0xFF80
        jle     N1S_45
        add     ax, ax
        mov     di, ax
        mov     ax, [di+56064]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_45
P1S_45:
        mov     di, 109
        call    FWD_SHIFT_ADD
        jmp     NXT_45
N1S_45:
        mov     di, 109
        call    FWD_SHIFT_SUB
NXT_45:
; ---- slot 46 ----
        mov     ax, ss:[bx+92]
        cmp     ax, 0x0080
        jge     P0S_46
        cmp     ax, 0xFF80
        jle     N0S_46
        add     ax, ax
        mov     di, ax
        mov     ax, [di+23808]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_46
P0S_46:
        mov     di, 46
        call    FWD_SHIFT_ADD
        jmp     P1E_46
N0S_46:
        mov     di, 46
        call    FWD_SHIFT_SUB
P1E_46:
        mov     ax, ss:[bp+92]
        cmp     ax, 0x0080
        jge     P1S_46
        cmp     ax, 0xFF80
        jle     N1S_46
        add     ax, ax
        mov     di, ax
        mov     ax, [di+56576]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_46
P1S_46:
        mov     di, 110
        call    FWD_SHIFT_ADD
        jmp     NXT_46
N1S_46:
        mov     di, 110
        call    FWD_SHIFT_SUB
NXT_46:
; ---- slot 47 ----
        mov     ax, ss:[bx+94]
        cmp     ax, 0x0080
        jge     P0S_47
        cmp     ax, 0xFF80
        jle     N0S_47
        add     ax, ax
        mov     di, ax
        mov     ax, [di+24320]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_47
P0S_47:
        mov     di, 47
        call    FWD_SHIFT_ADD
        jmp     P1E_47
N0S_47:
        mov     di, 47
        call    FWD_SHIFT_SUB
P1E_47:
        mov     ax, ss:[bp+94]
        cmp     ax, 0x0080
        jge     P1S_47
        cmp     ax, 0xFF80
        jle     N1S_47
        add     ax, ax
        mov     di, ax
        mov     ax, [di+57088]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_47
P1S_47:
        mov     di, 111
        call    FWD_SHIFT_ADD
        jmp     NXT_47
N1S_47:
        mov     di, 111
        call    FWD_SHIFT_SUB
NXT_47:
; ---- slot 48 ----
        mov     ax, ss:[bx+96]
        cmp     ax, 0x0080
        jge     P0S_48
        cmp     ax, 0xFF80
        jle     N0S_48
        add     ax, ax
        mov     di, ax
        mov     ax, [di+24832]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_48
P0S_48:
        mov     di, 48
        call    FWD_SHIFT_ADD
        jmp     P1E_48
N0S_48:
        mov     di, 48
        call    FWD_SHIFT_SUB
P1E_48:
        mov     ax, ss:[bp+96]
        cmp     ax, 0x0080
        jge     P1S_48
        cmp     ax, 0xFF80
        jle     N1S_48
        add     ax, ax
        mov     di, ax
        mov     ax, [di+57600]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_48
P1S_48:
        mov     di, 112
        call    FWD_SHIFT_ADD
        jmp     NXT_48
N1S_48:
        mov     di, 112
        call    FWD_SHIFT_SUB
NXT_48:
; ---- slot 49 ----
        mov     ax, ss:[bx+98]
        cmp     ax, 0x0080
        jge     P0S_49
        cmp     ax, 0xFF80
        jle     N0S_49
        add     ax, ax
        mov     di, ax
        mov     ax, [di+25344]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_49
P0S_49:
        mov     di, 49
        call    FWD_SHIFT_ADD
        jmp     P1E_49
N0S_49:
        mov     di, 49
        call    FWD_SHIFT_SUB
P1E_49:
        mov     ax, ss:[bp+98]
        cmp     ax, 0x0080
        jge     P1S_49
        cmp     ax, 0xFF80
        jle     N1S_49
        add     ax, ax
        mov     di, ax
        mov     ax, [di+58112]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_49
P1S_49:
        mov     di, 113
        call    FWD_SHIFT_ADD
        jmp     NXT_49
N1S_49:
        mov     di, 113
        call    FWD_SHIFT_SUB
NXT_49:
; ---- slot 50 ----
        mov     ax, ss:[bx+100]
        cmp     ax, 0x0080
        jge     P0S_50
        cmp     ax, 0xFF80
        jle     N0S_50
        add     ax, ax
        mov     di, ax
        mov     ax, [di+25856]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_50
P0S_50:
        mov     di, 50
        call    FWD_SHIFT_ADD
        jmp     P1E_50
N0S_50:
        mov     di, 50
        call    FWD_SHIFT_SUB
P1E_50:
        mov     ax, ss:[bp+100]
        cmp     ax, 0x0080
        jge     P1S_50
        cmp     ax, 0xFF80
        jle     N1S_50
        add     ax, ax
        mov     di, ax
        mov     ax, [di+58624]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_50
P1S_50:
        mov     di, 114
        call    FWD_SHIFT_ADD
        jmp     NXT_50
N1S_50:
        mov     di, 114
        call    FWD_SHIFT_SUB
NXT_50:
; ---- slot 51 ----
        mov     ax, ss:[bx+102]
        cmp     ax, 0x0080
        jge     P0S_51
        cmp     ax, 0xFF80
        jle     N0S_51
        add     ax, ax
        mov     di, ax
        mov     ax, [di+26368]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_51
P0S_51:
        mov     di, 51
        call    FWD_SHIFT_ADD
        jmp     P1E_51
N0S_51:
        mov     di, 51
        call    FWD_SHIFT_SUB
P1E_51:
        mov     ax, ss:[bp+102]
        cmp     ax, 0x0080
        jge     P1S_51
        cmp     ax, 0xFF80
        jle     N1S_51
        add     ax, ax
        mov     di, ax
        mov     ax, [di+59136]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_51
P1S_51:
        mov     di, 115
        call    FWD_SHIFT_ADD
        jmp     NXT_51
N1S_51:
        mov     di, 115
        call    FWD_SHIFT_SUB
NXT_51:
; ---- slot 52 ----
        mov     ax, ss:[bx+104]
        cmp     ax, 0x0080
        jge     P0S_52
        cmp     ax, 0xFF80
        jle     N0S_52
        add     ax, ax
        mov     di, ax
        mov     ax, [di+26880]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_52
P0S_52:
        mov     di, 52
        call    FWD_SHIFT_ADD
        jmp     P1E_52
N0S_52:
        mov     di, 52
        call    FWD_SHIFT_SUB
P1E_52:
        mov     ax, ss:[bp+104]
        cmp     ax, 0x0080
        jge     P1S_52
        cmp     ax, 0xFF80
        jle     N1S_52
        add     ax, ax
        mov     di, ax
        mov     ax, [di+59648]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_52
P1S_52:
        mov     di, 116
        call    FWD_SHIFT_ADD
        jmp     NXT_52
N1S_52:
        mov     di, 116
        call    FWD_SHIFT_SUB
NXT_52:
; ---- slot 53 ----
        mov     ax, ss:[bx+106]
        cmp     ax, 0x0080
        jge     P0S_53
        cmp     ax, 0xFF80
        jle     N0S_53
        add     ax, ax
        mov     di, ax
        mov     ax, [di+27392]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_53
P0S_53:
        mov     di, 53
        call    FWD_SHIFT_ADD
        jmp     P1E_53
N0S_53:
        mov     di, 53
        call    FWD_SHIFT_SUB
P1E_53:
        mov     ax, ss:[bp+106]
        cmp     ax, 0x0080
        jge     P1S_53
        cmp     ax, 0xFF80
        jle     N1S_53
        add     ax, ax
        mov     di, ax
        mov     ax, [di+60160]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_53
P1S_53:
        mov     di, 117
        call    FWD_SHIFT_ADD
        jmp     NXT_53
N1S_53:
        mov     di, 117
        call    FWD_SHIFT_SUB
NXT_53:
; ---- slot 54 ----
        mov     ax, ss:[bx+108]
        cmp     ax, 0x0080
        jge     P0S_54
        cmp     ax, 0xFF80
        jle     N0S_54
        add     ax, ax
        mov     di, ax
        mov     ax, [di+27904]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_54
P0S_54:
        mov     di, 54
        call    FWD_SHIFT_ADD
        jmp     P1E_54
N0S_54:
        mov     di, 54
        call    FWD_SHIFT_SUB
P1E_54:
        mov     ax, ss:[bp+108]
        cmp     ax, 0x0080
        jge     P1S_54
        cmp     ax, 0xFF80
        jle     N1S_54
        add     ax, ax
        mov     di, ax
        mov     ax, [di+60672]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_54
P1S_54:
        mov     di, 118
        call    FWD_SHIFT_ADD
        jmp     NXT_54
N1S_54:
        mov     di, 118
        call    FWD_SHIFT_SUB
NXT_54:
; ---- slot 55 ----
        mov     ax, ss:[bx+110]
        cmp     ax, 0x0080
        jge     P0S_55
        cmp     ax, 0xFF80
        jle     N0S_55
        add     ax, ax
        mov     di, ax
        mov     ax, [di+28416]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_55
P0S_55:
        mov     di, 55
        call    FWD_SHIFT_ADD
        jmp     P1E_55
N0S_55:
        mov     di, 55
        call    FWD_SHIFT_SUB
P1E_55:
        mov     ax, ss:[bp+110]
        cmp     ax, 0x0080
        jge     P1S_55
        cmp     ax, 0xFF80
        jle     N1S_55
        add     ax, ax
        mov     di, ax
        mov     ax, [di+61184]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_55
P1S_55:
        mov     di, 119
        call    FWD_SHIFT_ADD
        jmp     NXT_55
N1S_55:
        mov     di, 119
        call    FWD_SHIFT_SUB
NXT_55:
; ---- slot 56 ----
        mov     ax, ss:[bx+112]
        cmp     ax, 0x0080
        jge     P0S_56
        cmp     ax, 0xFF80
        jle     N0S_56
        add     ax, ax
        mov     di, ax
        mov     ax, [di+28928]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_56
P0S_56:
        mov     di, 56
        call    FWD_SHIFT_ADD
        jmp     P1E_56
N0S_56:
        mov     di, 56
        call    FWD_SHIFT_SUB
P1E_56:
        mov     ax, ss:[bp+112]
        cmp     ax, 0x0080
        jge     P1S_56
        cmp     ax, 0xFF80
        jle     N1S_56
        add     ax, ax
        mov     di, ax
        mov     ax, [di+61696]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_56
P1S_56:
        mov     di, 120
        call    FWD_SHIFT_ADD
        jmp     NXT_56
N1S_56:
        mov     di, 120
        call    FWD_SHIFT_SUB
NXT_56:
; ---- slot 57 ----
        mov     ax, ss:[bx+114]
        cmp     ax, 0x0080
        jge     P0S_57
        cmp     ax, 0xFF80
        jle     N0S_57
        add     ax, ax
        mov     di, ax
        mov     ax, [di+29440]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_57
P0S_57:
        mov     di, 57
        call    FWD_SHIFT_ADD
        jmp     P1E_57
N0S_57:
        mov     di, 57
        call    FWD_SHIFT_SUB
P1E_57:
        mov     ax, ss:[bp+114]
        cmp     ax, 0x0080
        jge     P1S_57
        cmp     ax, 0xFF80
        jle     N1S_57
        add     ax, ax
        mov     di, ax
        mov     ax, [di+62208]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_57
P1S_57:
        mov     di, 121
        call    FWD_SHIFT_ADD
        jmp     NXT_57
N1S_57:
        mov     di, 121
        call    FWD_SHIFT_SUB
NXT_57:
; ---- slot 58 ----
        mov     ax, ss:[bx+116]
        cmp     ax, 0x0080
        jge     P0S_58
        cmp     ax, 0xFF80
        jle     N0S_58
        add     ax, ax
        mov     di, ax
        mov     ax, [di+29952]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_58
P0S_58:
        mov     di, 58
        call    FWD_SHIFT_ADD
        jmp     P1E_58
N0S_58:
        mov     di, 58
        call    FWD_SHIFT_SUB
P1E_58:
        mov     ax, ss:[bp+116]
        cmp     ax, 0x0080
        jge     P1S_58
        cmp     ax, 0xFF80
        jle     N1S_58
        add     ax, ax
        mov     di, ax
        mov     ax, [di+62720]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_58
P1S_58:
        mov     di, 122
        call    FWD_SHIFT_ADD
        jmp     NXT_58
N1S_58:
        mov     di, 122
        call    FWD_SHIFT_SUB
NXT_58:
; ---- slot 59 ----
        mov     ax, ss:[bx+118]
        cmp     ax, 0x0080
        jge     P0S_59
        cmp     ax, 0xFF80
        jle     N0S_59
        add     ax, ax
        mov     di, ax
        mov     ax, [di+30464]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_59
P0S_59:
        mov     di, 59
        call    FWD_SHIFT_ADD
        jmp     P1E_59
N0S_59:
        mov     di, 59
        call    FWD_SHIFT_SUB
P1E_59:
        mov     ax, ss:[bp+118]
        cmp     ax, 0x0080
        jge     P1S_59
        cmp     ax, 0xFF80
        jle     N1S_59
        add     ax, ax
        mov     di, ax
        mov     ax, [di+63232]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_59
P1S_59:
        mov     di, 123
        call    FWD_SHIFT_ADD
        jmp     NXT_59
N1S_59:
        mov     di, 123
        call    FWD_SHIFT_SUB
NXT_59:
; ---- slot 60 ----
        mov     ax, ss:[bx+120]
        cmp     ax, 0x0080
        jge     P0S_60
        cmp     ax, 0xFF80
        jle     N0S_60
        add     ax, ax
        mov     di, ax
        mov     ax, [di+30976]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_60
P0S_60:
        mov     di, 60
        call    FWD_SHIFT_ADD
        jmp     P1E_60
N0S_60:
        mov     di, 60
        call    FWD_SHIFT_SUB
P1E_60:
        mov     ax, ss:[bp+120]
        cmp     ax, 0x0080
        jge     P1S_60
        cmp     ax, 0xFF80
        jle     N1S_60
        add     ax, ax
        mov     di, ax
        mov     ax, [di+63744]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_60
P1S_60:
        mov     di, 124
        call    FWD_SHIFT_ADD
        jmp     NXT_60
N1S_60:
        mov     di, 124
        call    FWD_SHIFT_SUB
NXT_60:
; ---- slot 61 ----
        mov     ax, ss:[bx+122]
        cmp     ax, 0x0080
        jge     P0S_61
        cmp     ax, 0xFF80
        jle     N0S_61
        add     ax, ax
        mov     di, ax
        mov     ax, [di+31488]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_61
P0S_61:
        mov     di, 61
        call    FWD_SHIFT_ADD
        jmp     P1E_61
N0S_61:
        mov     di, 61
        call    FWD_SHIFT_SUB
P1E_61:
        mov     ax, ss:[bp+122]
        cmp     ax, 0x0080
        jge     P1S_61
        cmp     ax, 0xFF80
        jle     N1S_61
        add     ax, ax
        mov     di, ax
        mov     ax, [di+64256]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_61
P1S_61:
        mov     di, 125
        call    FWD_SHIFT_ADD
        jmp     NXT_61
N1S_61:
        mov     di, 125
        call    FWD_SHIFT_SUB
NXT_61:
; ---- slot 62 ----
        mov     ax, ss:[bx+124]
        cmp     ax, 0x0080
        jge     P0S_62
        cmp     ax, 0xFF80
        jle     N0S_62
        add     ax, ax
        mov     di, ax
        mov     ax, [di+32000]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_62
P0S_62:
        mov     di, 62
        call    FWD_SHIFT_ADD
        jmp     P1E_62
N0S_62:
        mov     di, 62
        call    FWD_SHIFT_SUB
P1E_62:
        mov     ax, ss:[bp+124]
        cmp     ax, 0x0080
        jge     P1S_62
        cmp     ax, 0xFF80
        jle     N1S_62
        add     ax, ax
        mov     di, ax
        mov     ax, [di+64768]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_62
P1S_62:
        mov     di, 126
        call    FWD_SHIFT_ADD
        jmp     NXT_62
N1S_62:
        mov     di, 126
        call    FWD_SHIFT_SUB
NXT_62:
; ---- slot 63 ----
        mov     ax, ss:[bx+126]
        cmp     ax, 0x0080
        jge     P0S_63
        cmp     ax, 0xFF80
        jle     N0S_63
        add     ax, ax
        mov     di, ax
        mov     ax, [di+32512]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     P1E_63
P0S_63:
        mov     di, 63
        call    FWD_SHIFT_ADD
        jmp     P1E_63
N0S_63:
        mov     di, 63
        call    FWD_SHIFT_SUB
P1E_63:
        mov     ax, ss:[bp+126]
        cmp     ax, 0x0080
        jge     P1S_63
        cmp     ax, 0xFF80
        jle     N1S_63
        add     ax, ax
        mov     di, ax
        mov     ax, [di+65280]
        cwd
        add     si, ax
        adc     cx, dx
        jmp     NXT_63
P1S_63:
        mov     di, 127
        call    FWD_SHIFT_ADD
        jmp     NXT_63
N1S_63:
        mov     di, 127
        call    FWD_SHIFT_SUB
NXT_63:
; --- finish: (si:cx >> NNUE_SCALE_SHIFT) low word (score from stm's POV) ---
        mov     ax, cx
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        mov     dx, si
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        or      ax, dx
        pop     ds
        pop     di
        pop     si
        pop     bx
        pop     bp
        retf
nn_fwd_eval_ ENDP

; =====================================================================
; nn_fwd_eval2_ - NNUE forward pass, v2 (ReLU^2) via per-slot product
; tables: act = clamp(acc, 0, 255) and fwd[p][j][act] = (act^2 * w2) >> 8
; (the squared product is pre-shifted so every entry fits i16 - max
; 255^2*127>>8 = 32258). The symmetric clamp's shift-only +/-128 paths do
; NOT exist here: every term is a plain table load, so the cost is slightly
; BELOW v1. Table layout is the shared far array fwd[2][64][256]: fwd[0][j]
; base = j*512, fwd[1][j] base = 32768 + j*512 (persp 1 starts at +32768).
; si:cx = the 32-bit accumulator seeded from the bias; the finish shifts it
; right 5 (1.0 = 256 cp) exactly like v1. Calling convention: FAR, ax=side.
; Preserves bx,bp,si,di,ds; clobbers ax,dx,cx,es.
; GENERATED by gen_nnue_batch.py - edit the script, not this block.
nn_fwd_eval2_ PROC FAR
        push    bp
        push    bx
        push    si
        push    di
        push    ds
        mov     bx, OFFSET _nn_acc
        mov     cx, ss:_nn_ply
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        add     bx, cx
        mov     dx, ax
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        mov     bp, bx
        add     bp, 128
        sub     bp, dx
        add     bx, dx
        mov     ax, SEG _nn_fwd
        mov     ds, ax
        mov     ax, ss:_nn_bias
        cwd
        mov     si, ax
        mov     cx, dx
; ---- slot 0 ----
        mov     ax, ss:[bx+0]
        xor     di, di
        test    ax, ax
        jle     V2DN0_0
        cmp     ax, 255
        jle     V2OK0_0
        mov     ax, 255
V2OK0_0:
        add     ax, ax
        mov     di, ax
V2DN0_0:
        mov     ax, [di+0]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+0]
        xor     di, di
        test    ax, ax
        jle     V2DN1_0
        cmp     ax, 255
        jle     V2OK1_0
        mov     ax, 255
V2OK1_0:
        add     ax, ax
        mov     di, ax
V2DN1_0:
        mov     ax, [di+32768]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 1 ----
        mov     ax, ss:[bx+2]
        xor     di, di
        test    ax, ax
        jle     V2DN0_1
        cmp     ax, 255
        jle     V2OK0_1
        mov     ax, 255
V2OK0_1:
        add     ax, ax
        mov     di, ax
V2DN0_1:
        mov     ax, [di+512]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+2]
        xor     di, di
        test    ax, ax
        jle     V2DN1_1
        cmp     ax, 255
        jle     V2OK1_1
        mov     ax, 255
V2OK1_1:
        add     ax, ax
        mov     di, ax
V2DN1_1:
        mov     ax, [di+33280]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 2 ----
        mov     ax, ss:[bx+4]
        xor     di, di
        test    ax, ax
        jle     V2DN0_2
        cmp     ax, 255
        jle     V2OK0_2
        mov     ax, 255
V2OK0_2:
        add     ax, ax
        mov     di, ax
V2DN0_2:
        mov     ax, [di+1024]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+4]
        xor     di, di
        test    ax, ax
        jle     V2DN1_2
        cmp     ax, 255
        jle     V2OK1_2
        mov     ax, 255
V2OK1_2:
        add     ax, ax
        mov     di, ax
V2DN1_2:
        mov     ax, [di+33792]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 3 ----
        mov     ax, ss:[bx+6]
        xor     di, di
        test    ax, ax
        jle     V2DN0_3
        cmp     ax, 255
        jle     V2OK0_3
        mov     ax, 255
V2OK0_3:
        add     ax, ax
        mov     di, ax
V2DN0_3:
        mov     ax, [di+1536]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+6]
        xor     di, di
        test    ax, ax
        jle     V2DN1_3
        cmp     ax, 255
        jle     V2OK1_3
        mov     ax, 255
V2OK1_3:
        add     ax, ax
        mov     di, ax
V2DN1_3:
        mov     ax, [di+34304]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 4 ----
        mov     ax, ss:[bx+8]
        xor     di, di
        test    ax, ax
        jle     V2DN0_4
        cmp     ax, 255
        jle     V2OK0_4
        mov     ax, 255
V2OK0_4:
        add     ax, ax
        mov     di, ax
V2DN0_4:
        mov     ax, [di+2048]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+8]
        xor     di, di
        test    ax, ax
        jle     V2DN1_4
        cmp     ax, 255
        jle     V2OK1_4
        mov     ax, 255
V2OK1_4:
        add     ax, ax
        mov     di, ax
V2DN1_4:
        mov     ax, [di+34816]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 5 ----
        mov     ax, ss:[bx+10]
        xor     di, di
        test    ax, ax
        jle     V2DN0_5
        cmp     ax, 255
        jle     V2OK0_5
        mov     ax, 255
V2OK0_5:
        add     ax, ax
        mov     di, ax
V2DN0_5:
        mov     ax, [di+2560]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+10]
        xor     di, di
        test    ax, ax
        jle     V2DN1_5
        cmp     ax, 255
        jle     V2OK1_5
        mov     ax, 255
V2OK1_5:
        add     ax, ax
        mov     di, ax
V2DN1_5:
        mov     ax, [di+35328]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 6 ----
        mov     ax, ss:[bx+12]
        xor     di, di
        test    ax, ax
        jle     V2DN0_6
        cmp     ax, 255
        jle     V2OK0_6
        mov     ax, 255
V2OK0_6:
        add     ax, ax
        mov     di, ax
V2DN0_6:
        mov     ax, [di+3072]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+12]
        xor     di, di
        test    ax, ax
        jle     V2DN1_6
        cmp     ax, 255
        jle     V2OK1_6
        mov     ax, 255
V2OK1_6:
        add     ax, ax
        mov     di, ax
V2DN1_6:
        mov     ax, [di+35840]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 7 ----
        mov     ax, ss:[bx+14]
        xor     di, di
        test    ax, ax
        jle     V2DN0_7
        cmp     ax, 255
        jle     V2OK0_7
        mov     ax, 255
V2OK0_7:
        add     ax, ax
        mov     di, ax
V2DN0_7:
        mov     ax, [di+3584]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+14]
        xor     di, di
        test    ax, ax
        jle     V2DN1_7
        cmp     ax, 255
        jle     V2OK1_7
        mov     ax, 255
V2OK1_7:
        add     ax, ax
        mov     di, ax
V2DN1_7:
        mov     ax, [di+36352]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 8 ----
        mov     ax, ss:[bx+16]
        xor     di, di
        test    ax, ax
        jle     V2DN0_8
        cmp     ax, 255
        jle     V2OK0_8
        mov     ax, 255
V2OK0_8:
        add     ax, ax
        mov     di, ax
V2DN0_8:
        mov     ax, [di+4096]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+16]
        xor     di, di
        test    ax, ax
        jle     V2DN1_8
        cmp     ax, 255
        jle     V2OK1_8
        mov     ax, 255
V2OK1_8:
        add     ax, ax
        mov     di, ax
V2DN1_8:
        mov     ax, [di+36864]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 9 ----
        mov     ax, ss:[bx+18]
        xor     di, di
        test    ax, ax
        jle     V2DN0_9
        cmp     ax, 255
        jle     V2OK0_9
        mov     ax, 255
V2OK0_9:
        add     ax, ax
        mov     di, ax
V2DN0_9:
        mov     ax, [di+4608]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+18]
        xor     di, di
        test    ax, ax
        jle     V2DN1_9
        cmp     ax, 255
        jle     V2OK1_9
        mov     ax, 255
V2OK1_9:
        add     ax, ax
        mov     di, ax
V2DN1_9:
        mov     ax, [di+37376]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 10 ----
        mov     ax, ss:[bx+20]
        xor     di, di
        test    ax, ax
        jle     V2DN0_10
        cmp     ax, 255
        jle     V2OK0_10
        mov     ax, 255
V2OK0_10:
        add     ax, ax
        mov     di, ax
V2DN0_10:
        mov     ax, [di+5120]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+20]
        xor     di, di
        test    ax, ax
        jle     V2DN1_10
        cmp     ax, 255
        jle     V2OK1_10
        mov     ax, 255
V2OK1_10:
        add     ax, ax
        mov     di, ax
V2DN1_10:
        mov     ax, [di+37888]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 11 ----
        mov     ax, ss:[bx+22]
        xor     di, di
        test    ax, ax
        jle     V2DN0_11
        cmp     ax, 255
        jle     V2OK0_11
        mov     ax, 255
V2OK0_11:
        add     ax, ax
        mov     di, ax
V2DN0_11:
        mov     ax, [di+5632]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+22]
        xor     di, di
        test    ax, ax
        jle     V2DN1_11
        cmp     ax, 255
        jle     V2OK1_11
        mov     ax, 255
V2OK1_11:
        add     ax, ax
        mov     di, ax
V2DN1_11:
        mov     ax, [di+38400]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 12 ----
        mov     ax, ss:[bx+24]
        xor     di, di
        test    ax, ax
        jle     V2DN0_12
        cmp     ax, 255
        jle     V2OK0_12
        mov     ax, 255
V2OK0_12:
        add     ax, ax
        mov     di, ax
V2DN0_12:
        mov     ax, [di+6144]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+24]
        xor     di, di
        test    ax, ax
        jle     V2DN1_12
        cmp     ax, 255
        jle     V2OK1_12
        mov     ax, 255
V2OK1_12:
        add     ax, ax
        mov     di, ax
V2DN1_12:
        mov     ax, [di+38912]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 13 ----
        mov     ax, ss:[bx+26]
        xor     di, di
        test    ax, ax
        jle     V2DN0_13
        cmp     ax, 255
        jle     V2OK0_13
        mov     ax, 255
V2OK0_13:
        add     ax, ax
        mov     di, ax
V2DN0_13:
        mov     ax, [di+6656]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+26]
        xor     di, di
        test    ax, ax
        jle     V2DN1_13
        cmp     ax, 255
        jle     V2OK1_13
        mov     ax, 255
V2OK1_13:
        add     ax, ax
        mov     di, ax
V2DN1_13:
        mov     ax, [di+39424]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 14 ----
        mov     ax, ss:[bx+28]
        xor     di, di
        test    ax, ax
        jle     V2DN0_14
        cmp     ax, 255
        jle     V2OK0_14
        mov     ax, 255
V2OK0_14:
        add     ax, ax
        mov     di, ax
V2DN0_14:
        mov     ax, [di+7168]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+28]
        xor     di, di
        test    ax, ax
        jle     V2DN1_14
        cmp     ax, 255
        jle     V2OK1_14
        mov     ax, 255
V2OK1_14:
        add     ax, ax
        mov     di, ax
V2DN1_14:
        mov     ax, [di+39936]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 15 ----
        mov     ax, ss:[bx+30]
        xor     di, di
        test    ax, ax
        jle     V2DN0_15
        cmp     ax, 255
        jle     V2OK0_15
        mov     ax, 255
V2OK0_15:
        add     ax, ax
        mov     di, ax
V2DN0_15:
        mov     ax, [di+7680]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+30]
        xor     di, di
        test    ax, ax
        jle     V2DN1_15
        cmp     ax, 255
        jle     V2OK1_15
        mov     ax, 255
V2OK1_15:
        add     ax, ax
        mov     di, ax
V2DN1_15:
        mov     ax, [di+40448]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 16 ----
        mov     ax, ss:[bx+32]
        xor     di, di
        test    ax, ax
        jle     V2DN0_16
        cmp     ax, 255
        jle     V2OK0_16
        mov     ax, 255
V2OK0_16:
        add     ax, ax
        mov     di, ax
V2DN0_16:
        mov     ax, [di+8192]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+32]
        xor     di, di
        test    ax, ax
        jle     V2DN1_16
        cmp     ax, 255
        jle     V2OK1_16
        mov     ax, 255
V2OK1_16:
        add     ax, ax
        mov     di, ax
V2DN1_16:
        mov     ax, [di+40960]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 17 ----
        mov     ax, ss:[bx+34]
        xor     di, di
        test    ax, ax
        jle     V2DN0_17
        cmp     ax, 255
        jle     V2OK0_17
        mov     ax, 255
V2OK0_17:
        add     ax, ax
        mov     di, ax
V2DN0_17:
        mov     ax, [di+8704]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+34]
        xor     di, di
        test    ax, ax
        jle     V2DN1_17
        cmp     ax, 255
        jle     V2OK1_17
        mov     ax, 255
V2OK1_17:
        add     ax, ax
        mov     di, ax
V2DN1_17:
        mov     ax, [di+41472]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 18 ----
        mov     ax, ss:[bx+36]
        xor     di, di
        test    ax, ax
        jle     V2DN0_18
        cmp     ax, 255
        jle     V2OK0_18
        mov     ax, 255
V2OK0_18:
        add     ax, ax
        mov     di, ax
V2DN0_18:
        mov     ax, [di+9216]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+36]
        xor     di, di
        test    ax, ax
        jle     V2DN1_18
        cmp     ax, 255
        jle     V2OK1_18
        mov     ax, 255
V2OK1_18:
        add     ax, ax
        mov     di, ax
V2DN1_18:
        mov     ax, [di+41984]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 19 ----
        mov     ax, ss:[bx+38]
        xor     di, di
        test    ax, ax
        jle     V2DN0_19
        cmp     ax, 255
        jle     V2OK0_19
        mov     ax, 255
V2OK0_19:
        add     ax, ax
        mov     di, ax
V2DN0_19:
        mov     ax, [di+9728]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+38]
        xor     di, di
        test    ax, ax
        jle     V2DN1_19
        cmp     ax, 255
        jle     V2OK1_19
        mov     ax, 255
V2OK1_19:
        add     ax, ax
        mov     di, ax
V2DN1_19:
        mov     ax, [di+42496]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 20 ----
        mov     ax, ss:[bx+40]
        xor     di, di
        test    ax, ax
        jle     V2DN0_20
        cmp     ax, 255
        jle     V2OK0_20
        mov     ax, 255
V2OK0_20:
        add     ax, ax
        mov     di, ax
V2DN0_20:
        mov     ax, [di+10240]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+40]
        xor     di, di
        test    ax, ax
        jle     V2DN1_20
        cmp     ax, 255
        jle     V2OK1_20
        mov     ax, 255
V2OK1_20:
        add     ax, ax
        mov     di, ax
V2DN1_20:
        mov     ax, [di+43008]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 21 ----
        mov     ax, ss:[bx+42]
        xor     di, di
        test    ax, ax
        jle     V2DN0_21
        cmp     ax, 255
        jle     V2OK0_21
        mov     ax, 255
V2OK0_21:
        add     ax, ax
        mov     di, ax
V2DN0_21:
        mov     ax, [di+10752]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+42]
        xor     di, di
        test    ax, ax
        jle     V2DN1_21
        cmp     ax, 255
        jle     V2OK1_21
        mov     ax, 255
V2OK1_21:
        add     ax, ax
        mov     di, ax
V2DN1_21:
        mov     ax, [di+43520]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 22 ----
        mov     ax, ss:[bx+44]
        xor     di, di
        test    ax, ax
        jle     V2DN0_22
        cmp     ax, 255
        jle     V2OK0_22
        mov     ax, 255
V2OK0_22:
        add     ax, ax
        mov     di, ax
V2DN0_22:
        mov     ax, [di+11264]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+44]
        xor     di, di
        test    ax, ax
        jle     V2DN1_22
        cmp     ax, 255
        jle     V2OK1_22
        mov     ax, 255
V2OK1_22:
        add     ax, ax
        mov     di, ax
V2DN1_22:
        mov     ax, [di+44032]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 23 ----
        mov     ax, ss:[bx+46]
        xor     di, di
        test    ax, ax
        jle     V2DN0_23
        cmp     ax, 255
        jle     V2OK0_23
        mov     ax, 255
V2OK0_23:
        add     ax, ax
        mov     di, ax
V2DN0_23:
        mov     ax, [di+11776]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+46]
        xor     di, di
        test    ax, ax
        jle     V2DN1_23
        cmp     ax, 255
        jle     V2OK1_23
        mov     ax, 255
V2OK1_23:
        add     ax, ax
        mov     di, ax
V2DN1_23:
        mov     ax, [di+44544]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 24 ----
        mov     ax, ss:[bx+48]
        xor     di, di
        test    ax, ax
        jle     V2DN0_24
        cmp     ax, 255
        jle     V2OK0_24
        mov     ax, 255
V2OK0_24:
        add     ax, ax
        mov     di, ax
V2DN0_24:
        mov     ax, [di+12288]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+48]
        xor     di, di
        test    ax, ax
        jle     V2DN1_24
        cmp     ax, 255
        jle     V2OK1_24
        mov     ax, 255
V2OK1_24:
        add     ax, ax
        mov     di, ax
V2DN1_24:
        mov     ax, [di+45056]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 25 ----
        mov     ax, ss:[bx+50]
        xor     di, di
        test    ax, ax
        jle     V2DN0_25
        cmp     ax, 255
        jle     V2OK0_25
        mov     ax, 255
V2OK0_25:
        add     ax, ax
        mov     di, ax
V2DN0_25:
        mov     ax, [di+12800]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+50]
        xor     di, di
        test    ax, ax
        jle     V2DN1_25
        cmp     ax, 255
        jle     V2OK1_25
        mov     ax, 255
V2OK1_25:
        add     ax, ax
        mov     di, ax
V2DN1_25:
        mov     ax, [di+45568]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 26 ----
        mov     ax, ss:[bx+52]
        xor     di, di
        test    ax, ax
        jle     V2DN0_26
        cmp     ax, 255
        jle     V2OK0_26
        mov     ax, 255
V2OK0_26:
        add     ax, ax
        mov     di, ax
V2DN0_26:
        mov     ax, [di+13312]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+52]
        xor     di, di
        test    ax, ax
        jle     V2DN1_26
        cmp     ax, 255
        jle     V2OK1_26
        mov     ax, 255
V2OK1_26:
        add     ax, ax
        mov     di, ax
V2DN1_26:
        mov     ax, [di+46080]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 27 ----
        mov     ax, ss:[bx+54]
        xor     di, di
        test    ax, ax
        jle     V2DN0_27
        cmp     ax, 255
        jle     V2OK0_27
        mov     ax, 255
V2OK0_27:
        add     ax, ax
        mov     di, ax
V2DN0_27:
        mov     ax, [di+13824]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+54]
        xor     di, di
        test    ax, ax
        jle     V2DN1_27
        cmp     ax, 255
        jle     V2OK1_27
        mov     ax, 255
V2OK1_27:
        add     ax, ax
        mov     di, ax
V2DN1_27:
        mov     ax, [di+46592]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 28 ----
        mov     ax, ss:[bx+56]
        xor     di, di
        test    ax, ax
        jle     V2DN0_28
        cmp     ax, 255
        jle     V2OK0_28
        mov     ax, 255
V2OK0_28:
        add     ax, ax
        mov     di, ax
V2DN0_28:
        mov     ax, [di+14336]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+56]
        xor     di, di
        test    ax, ax
        jle     V2DN1_28
        cmp     ax, 255
        jle     V2OK1_28
        mov     ax, 255
V2OK1_28:
        add     ax, ax
        mov     di, ax
V2DN1_28:
        mov     ax, [di+47104]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 29 ----
        mov     ax, ss:[bx+58]
        xor     di, di
        test    ax, ax
        jle     V2DN0_29
        cmp     ax, 255
        jle     V2OK0_29
        mov     ax, 255
V2OK0_29:
        add     ax, ax
        mov     di, ax
V2DN0_29:
        mov     ax, [di+14848]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+58]
        xor     di, di
        test    ax, ax
        jle     V2DN1_29
        cmp     ax, 255
        jle     V2OK1_29
        mov     ax, 255
V2OK1_29:
        add     ax, ax
        mov     di, ax
V2DN1_29:
        mov     ax, [di+47616]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 30 ----
        mov     ax, ss:[bx+60]
        xor     di, di
        test    ax, ax
        jle     V2DN0_30
        cmp     ax, 255
        jle     V2OK0_30
        mov     ax, 255
V2OK0_30:
        add     ax, ax
        mov     di, ax
V2DN0_30:
        mov     ax, [di+15360]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+60]
        xor     di, di
        test    ax, ax
        jle     V2DN1_30
        cmp     ax, 255
        jle     V2OK1_30
        mov     ax, 255
V2OK1_30:
        add     ax, ax
        mov     di, ax
V2DN1_30:
        mov     ax, [di+48128]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 31 ----
        mov     ax, ss:[bx+62]
        xor     di, di
        test    ax, ax
        jle     V2DN0_31
        cmp     ax, 255
        jle     V2OK0_31
        mov     ax, 255
V2OK0_31:
        add     ax, ax
        mov     di, ax
V2DN0_31:
        mov     ax, [di+15872]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+62]
        xor     di, di
        test    ax, ax
        jle     V2DN1_31
        cmp     ax, 255
        jle     V2OK1_31
        mov     ax, 255
V2OK1_31:
        add     ax, ax
        mov     di, ax
V2DN1_31:
        mov     ax, [di+48640]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 32 ----
        mov     ax, ss:[bx+64]
        xor     di, di
        test    ax, ax
        jle     V2DN0_32
        cmp     ax, 255
        jle     V2OK0_32
        mov     ax, 255
V2OK0_32:
        add     ax, ax
        mov     di, ax
V2DN0_32:
        mov     ax, [di+16384]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+64]
        xor     di, di
        test    ax, ax
        jle     V2DN1_32
        cmp     ax, 255
        jle     V2OK1_32
        mov     ax, 255
V2OK1_32:
        add     ax, ax
        mov     di, ax
V2DN1_32:
        mov     ax, [di+49152]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 33 ----
        mov     ax, ss:[bx+66]
        xor     di, di
        test    ax, ax
        jle     V2DN0_33
        cmp     ax, 255
        jle     V2OK0_33
        mov     ax, 255
V2OK0_33:
        add     ax, ax
        mov     di, ax
V2DN0_33:
        mov     ax, [di+16896]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+66]
        xor     di, di
        test    ax, ax
        jle     V2DN1_33
        cmp     ax, 255
        jle     V2OK1_33
        mov     ax, 255
V2OK1_33:
        add     ax, ax
        mov     di, ax
V2DN1_33:
        mov     ax, [di+49664]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 34 ----
        mov     ax, ss:[bx+68]
        xor     di, di
        test    ax, ax
        jle     V2DN0_34
        cmp     ax, 255
        jle     V2OK0_34
        mov     ax, 255
V2OK0_34:
        add     ax, ax
        mov     di, ax
V2DN0_34:
        mov     ax, [di+17408]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+68]
        xor     di, di
        test    ax, ax
        jle     V2DN1_34
        cmp     ax, 255
        jle     V2OK1_34
        mov     ax, 255
V2OK1_34:
        add     ax, ax
        mov     di, ax
V2DN1_34:
        mov     ax, [di+50176]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 35 ----
        mov     ax, ss:[bx+70]
        xor     di, di
        test    ax, ax
        jle     V2DN0_35
        cmp     ax, 255
        jle     V2OK0_35
        mov     ax, 255
V2OK0_35:
        add     ax, ax
        mov     di, ax
V2DN0_35:
        mov     ax, [di+17920]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+70]
        xor     di, di
        test    ax, ax
        jle     V2DN1_35
        cmp     ax, 255
        jle     V2OK1_35
        mov     ax, 255
V2OK1_35:
        add     ax, ax
        mov     di, ax
V2DN1_35:
        mov     ax, [di+50688]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 36 ----
        mov     ax, ss:[bx+72]
        xor     di, di
        test    ax, ax
        jle     V2DN0_36
        cmp     ax, 255
        jle     V2OK0_36
        mov     ax, 255
V2OK0_36:
        add     ax, ax
        mov     di, ax
V2DN0_36:
        mov     ax, [di+18432]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+72]
        xor     di, di
        test    ax, ax
        jle     V2DN1_36
        cmp     ax, 255
        jle     V2OK1_36
        mov     ax, 255
V2OK1_36:
        add     ax, ax
        mov     di, ax
V2DN1_36:
        mov     ax, [di+51200]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 37 ----
        mov     ax, ss:[bx+74]
        xor     di, di
        test    ax, ax
        jle     V2DN0_37
        cmp     ax, 255
        jle     V2OK0_37
        mov     ax, 255
V2OK0_37:
        add     ax, ax
        mov     di, ax
V2DN0_37:
        mov     ax, [di+18944]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+74]
        xor     di, di
        test    ax, ax
        jle     V2DN1_37
        cmp     ax, 255
        jle     V2OK1_37
        mov     ax, 255
V2OK1_37:
        add     ax, ax
        mov     di, ax
V2DN1_37:
        mov     ax, [di+51712]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 38 ----
        mov     ax, ss:[bx+76]
        xor     di, di
        test    ax, ax
        jle     V2DN0_38
        cmp     ax, 255
        jle     V2OK0_38
        mov     ax, 255
V2OK0_38:
        add     ax, ax
        mov     di, ax
V2DN0_38:
        mov     ax, [di+19456]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+76]
        xor     di, di
        test    ax, ax
        jle     V2DN1_38
        cmp     ax, 255
        jle     V2OK1_38
        mov     ax, 255
V2OK1_38:
        add     ax, ax
        mov     di, ax
V2DN1_38:
        mov     ax, [di+52224]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 39 ----
        mov     ax, ss:[bx+78]
        xor     di, di
        test    ax, ax
        jle     V2DN0_39
        cmp     ax, 255
        jle     V2OK0_39
        mov     ax, 255
V2OK0_39:
        add     ax, ax
        mov     di, ax
V2DN0_39:
        mov     ax, [di+19968]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+78]
        xor     di, di
        test    ax, ax
        jle     V2DN1_39
        cmp     ax, 255
        jle     V2OK1_39
        mov     ax, 255
V2OK1_39:
        add     ax, ax
        mov     di, ax
V2DN1_39:
        mov     ax, [di+52736]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 40 ----
        mov     ax, ss:[bx+80]
        xor     di, di
        test    ax, ax
        jle     V2DN0_40
        cmp     ax, 255
        jle     V2OK0_40
        mov     ax, 255
V2OK0_40:
        add     ax, ax
        mov     di, ax
V2DN0_40:
        mov     ax, [di+20480]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+80]
        xor     di, di
        test    ax, ax
        jle     V2DN1_40
        cmp     ax, 255
        jle     V2OK1_40
        mov     ax, 255
V2OK1_40:
        add     ax, ax
        mov     di, ax
V2DN1_40:
        mov     ax, [di+53248]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 41 ----
        mov     ax, ss:[bx+82]
        xor     di, di
        test    ax, ax
        jle     V2DN0_41
        cmp     ax, 255
        jle     V2OK0_41
        mov     ax, 255
V2OK0_41:
        add     ax, ax
        mov     di, ax
V2DN0_41:
        mov     ax, [di+20992]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+82]
        xor     di, di
        test    ax, ax
        jle     V2DN1_41
        cmp     ax, 255
        jle     V2OK1_41
        mov     ax, 255
V2OK1_41:
        add     ax, ax
        mov     di, ax
V2DN1_41:
        mov     ax, [di+53760]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 42 ----
        mov     ax, ss:[bx+84]
        xor     di, di
        test    ax, ax
        jle     V2DN0_42
        cmp     ax, 255
        jle     V2OK0_42
        mov     ax, 255
V2OK0_42:
        add     ax, ax
        mov     di, ax
V2DN0_42:
        mov     ax, [di+21504]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+84]
        xor     di, di
        test    ax, ax
        jle     V2DN1_42
        cmp     ax, 255
        jle     V2OK1_42
        mov     ax, 255
V2OK1_42:
        add     ax, ax
        mov     di, ax
V2DN1_42:
        mov     ax, [di+54272]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 43 ----
        mov     ax, ss:[bx+86]
        xor     di, di
        test    ax, ax
        jle     V2DN0_43
        cmp     ax, 255
        jle     V2OK0_43
        mov     ax, 255
V2OK0_43:
        add     ax, ax
        mov     di, ax
V2DN0_43:
        mov     ax, [di+22016]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+86]
        xor     di, di
        test    ax, ax
        jle     V2DN1_43
        cmp     ax, 255
        jle     V2OK1_43
        mov     ax, 255
V2OK1_43:
        add     ax, ax
        mov     di, ax
V2DN1_43:
        mov     ax, [di+54784]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 44 ----
        mov     ax, ss:[bx+88]
        xor     di, di
        test    ax, ax
        jle     V2DN0_44
        cmp     ax, 255
        jle     V2OK0_44
        mov     ax, 255
V2OK0_44:
        add     ax, ax
        mov     di, ax
V2DN0_44:
        mov     ax, [di+22528]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+88]
        xor     di, di
        test    ax, ax
        jle     V2DN1_44
        cmp     ax, 255
        jle     V2OK1_44
        mov     ax, 255
V2OK1_44:
        add     ax, ax
        mov     di, ax
V2DN1_44:
        mov     ax, [di+55296]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 45 ----
        mov     ax, ss:[bx+90]
        xor     di, di
        test    ax, ax
        jle     V2DN0_45
        cmp     ax, 255
        jle     V2OK0_45
        mov     ax, 255
V2OK0_45:
        add     ax, ax
        mov     di, ax
V2DN0_45:
        mov     ax, [di+23040]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+90]
        xor     di, di
        test    ax, ax
        jle     V2DN1_45
        cmp     ax, 255
        jle     V2OK1_45
        mov     ax, 255
V2OK1_45:
        add     ax, ax
        mov     di, ax
V2DN1_45:
        mov     ax, [di+55808]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 46 ----
        mov     ax, ss:[bx+92]
        xor     di, di
        test    ax, ax
        jle     V2DN0_46
        cmp     ax, 255
        jle     V2OK0_46
        mov     ax, 255
V2OK0_46:
        add     ax, ax
        mov     di, ax
V2DN0_46:
        mov     ax, [di+23552]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+92]
        xor     di, di
        test    ax, ax
        jle     V2DN1_46
        cmp     ax, 255
        jle     V2OK1_46
        mov     ax, 255
V2OK1_46:
        add     ax, ax
        mov     di, ax
V2DN1_46:
        mov     ax, [di+56320]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 47 ----
        mov     ax, ss:[bx+94]
        xor     di, di
        test    ax, ax
        jle     V2DN0_47
        cmp     ax, 255
        jle     V2OK0_47
        mov     ax, 255
V2OK0_47:
        add     ax, ax
        mov     di, ax
V2DN0_47:
        mov     ax, [di+24064]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+94]
        xor     di, di
        test    ax, ax
        jle     V2DN1_47
        cmp     ax, 255
        jle     V2OK1_47
        mov     ax, 255
V2OK1_47:
        add     ax, ax
        mov     di, ax
V2DN1_47:
        mov     ax, [di+56832]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 48 ----
        mov     ax, ss:[bx+96]
        xor     di, di
        test    ax, ax
        jle     V2DN0_48
        cmp     ax, 255
        jle     V2OK0_48
        mov     ax, 255
V2OK0_48:
        add     ax, ax
        mov     di, ax
V2DN0_48:
        mov     ax, [di+24576]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+96]
        xor     di, di
        test    ax, ax
        jle     V2DN1_48
        cmp     ax, 255
        jle     V2OK1_48
        mov     ax, 255
V2OK1_48:
        add     ax, ax
        mov     di, ax
V2DN1_48:
        mov     ax, [di+57344]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 49 ----
        mov     ax, ss:[bx+98]
        xor     di, di
        test    ax, ax
        jle     V2DN0_49
        cmp     ax, 255
        jle     V2OK0_49
        mov     ax, 255
V2OK0_49:
        add     ax, ax
        mov     di, ax
V2DN0_49:
        mov     ax, [di+25088]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+98]
        xor     di, di
        test    ax, ax
        jle     V2DN1_49
        cmp     ax, 255
        jle     V2OK1_49
        mov     ax, 255
V2OK1_49:
        add     ax, ax
        mov     di, ax
V2DN1_49:
        mov     ax, [di+57856]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 50 ----
        mov     ax, ss:[bx+100]
        xor     di, di
        test    ax, ax
        jle     V2DN0_50
        cmp     ax, 255
        jle     V2OK0_50
        mov     ax, 255
V2OK0_50:
        add     ax, ax
        mov     di, ax
V2DN0_50:
        mov     ax, [di+25600]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+100]
        xor     di, di
        test    ax, ax
        jle     V2DN1_50
        cmp     ax, 255
        jle     V2OK1_50
        mov     ax, 255
V2OK1_50:
        add     ax, ax
        mov     di, ax
V2DN1_50:
        mov     ax, [di+58368]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 51 ----
        mov     ax, ss:[bx+102]
        xor     di, di
        test    ax, ax
        jle     V2DN0_51
        cmp     ax, 255
        jle     V2OK0_51
        mov     ax, 255
V2OK0_51:
        add     ax, ax
        mov     di, ax
V2DN0_51:
        mov     ax, [di+26112]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+102]
        xor     di, di
        test    ax, ax
        jle     V2DN1_51
        cmp     ax, 255
        jle     V2OK1_51
        mov     ax, 255
V2OK1_51:
        add     ax, ax
        mov     di, ax
V2DN1_51:
        mov     ax, [di+58880]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 52 ----
        mov     ax, ss:[bx+104]
        xor     di, di
        test    ax, ax
        jle     V2DN0_52
        cmp     ax, 255
        jle     V2OK0_52
        mov     ax, 255
V2OK0_52:
        add     ax, ax
        mov     di, ax
V2DN0_52:
        mov     ax, [di+26624]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+104]
        xor     di, di
        test    ax, ax
        jle     V2DN1_52
        cmp     ax, 255
        jle     V2OK1_52
        mov     ax, 255
V2OK1_52:
        add     ax, ax
        mov     di, ax
V2DN1_52:
        mov     ax, [di+59392]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 53 ----
        mov     ax, ss:[bx+106]
        xor     di, di
        test    ax, ax
        jle     V2DN0_53
        cmp     ax, 255
        jle     V2OK0_53
        mov     ax, 255
V2OK0_53:
        add     ax, ax
        mov     di, ax
V2DN0_53:
        mov     ax, [di+27136]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+106]
        xor     di, di
        test    ax, ax
        jle     V2DN1_53
        cmp     ax, 255
        jle     V2OK1_53
        mov     ax, 255
V2OK1_53:
        add     ax, ax
        mov     di, ax
V2DN1_53:
        mov     ax, [di+59904]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 54 ----
        mov     ax, ss:[bx+108]
        xor     di, di
        test    ax, ax
        jle     V2DN0_54
        cmp     ax, 255
        jle     V2OK0_54
        mov     ax, 255
V2OK0_54:
        add     ax, ax
        mov     di, ax
V2DN0_54:
        mov     ax, [di+27648]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+108]
        xor     di, di
        test    ax, ax
        jle     V2DN1_54
        cmp     ax, 255
        jle     V2OK1_54
        mov     ax, 255
V2OK1_54:
        add     ax, ax
        mov     di, ax
V2DN1_54:
        mov     ax, [di+60416]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 55 ----
        mov     ax, ss:[bx+110]
        xor     di, di
        test    ax, ax
        jle     V2DN0_55
        cmp     ax, 255
        jle     V2OK0_55
        mov     ax, 255
V2OK0_55:
        add     ax, ax
        mov     di, ax
V2DN0_55:
        mov     ax, [di+28160]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+110]
        xor     di, di
        test    ax, ax
        jle     V2DN1_55
        cmp     ax, 255
        jle     V2OK1_55
        mov     ax, 255
V2OK1_55:
        add     ax, ax
        mov     di, ax
V2DN1_55:
        mov     ax, [di+60928]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 56 ----
        mov     ax, ss:[bx+112]
        xor     di, di
        test    ax, ax
        jle     V2DN0_56
        cmp     ax, 255
        jle     V2OK0_56
        mov     ax, 255
V2OK0_56:
        add     ax, ax
        mov     di, ax
V2DN0_56:
        mov     ax, [di+28672]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+112]
        xor     di, di
        test    ax, ax
        jle     V2DN1_56
        cmp     ax, 255
        jle     V2OK1_56
        mov     ax, 255
V2OK1_56:
        add     ax, ax
        mov     di, ax
V2DN1_56:
        mov     ax, [di+61440]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 57 ----
        mov     ax, ss:[bx+114]
        xor     di, di
        test    ax, ax
        jle     V2DN0_57
        cmp     ax, 255
        jle     V2OK0_57
        mov     ax, 255
V2OK0_57:
        add     ax, ax
        mov     di, ax
V2DN0_57:
        mov     ax, [di+29184]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+114]
        xor     di, di
        test    ax, ax
        jle     V2DN1_57
        cmp     ax, 255
        jle     V2OK1_57
        mov     ax, 255
V2OK1_57:
        add     ax, ax
        mov     di, ax
V2DN1_57:
        mov     ax, [di+61952]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 58 ----
        mov     ax, ss:[bx+116]
        xor     di, di
        test    ax, ax
        jle     V2DN0_58
        cmp     ax, 255
        jle     V2OK0_58
        mov     ax, 255
V2OK0_58:
        add     ax, ax
        mov     di, ax
V2DN0_58:
        mov     ax, [di+29696]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+116]
        xor     di, di
        test    ax, ax
        jle     V2DN1_58
        cmp     ax, 255
        jle     V2OK1_58
        mov     ax, 255
V2OK1_58:
        add     ax, ax
        mov     di, ax
V2DN1_58:
        mov     ax, [di+62464]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 59 ----
        mov     ax, ss:[bx+118]
        xor     di, di
        test    ax, ax
        jle     V2DN0_59
        cmp     ax, 255
        jle     V2OK0_59
        mov     ax, 255
V2OK0_59:
        add     ax, ax
        mov     di, ax
V2DN0_59:
        mov     ax, [di+30208]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+118]
        xor     di, di
        test    ax, ax
        jle     V2DN1_59
        cmp     ax, 255
        jle     V2OK1_59
        mov     ax, 255
V2OK1_59:
        add     ax, ax
        mov     di, ax
V2DN1_59:
        mov     ax, [di+62976]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 60 ----
        mov     ax, ss:[bx+120]
        xor     di, di
        test    ax, ax
        jle     V2DN0_60
        cmp     ax, 255
        jle     V2OK0_60
        mov     ax, 255
V2OK0_60:
        add     ax, ax
        mov     di, ax
V2DN0_60:
        mov     ax, [di+30720]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+120]
        xor     di, di
        test    ax, ax
        jle     V2DN1_60
        cmp     ax, 255
        jle     V2OK1_60
        mov     ax, 255
V2OK1_60:
        add     ax, ax
        mov     di, ax
V2DN1_60:
        mov     ax, [di+63488]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 61 ----
        mov     ax, ss:[bx+122]
        xor     di, di
        test    ax, ax
        jle     V2DN0_61
        cmp     ax, 255
        jle     V2OK0_61
        mov     ax, 255
V2OK0_61:
        add     ax, ax
        mov     di, ax
V2DN0_61:
        mov     ax, [di+31232]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+122]
        xor     di, di
        test    ax, ax
        jle     V2DN1_61
        cmp     ax, 255
        jle     V2OK1_61
        mov     ax, 255
V2OK1_61:
        add     ax, ax
        mov     di, ax
V2DN1_61:
        mov     ax, [di+64000]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 62 ----
        mov     ax, ss:[bx+124]
        xor     di, di
        test    ax, ax
        jle     V2DN0_62
        cmp     ax, 255
        jle     V2OK0_62
        mov     ax, 255
V2OK0_62:
        add     ax, ax
        mov     di, ax
V2DN0_62:
        mov     ax, [di+31744]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+124]
        xor     di, di
        test    ax, ax
        jle     V2DN1_62
        cmp     ax, 255
        jle     V2OK1_62
        mov     ax, 255
V2OK1_62:
        add     ax, ax
        mov     di, ax
V2DN1_62:
        mov     ax, [di+64512]
        cwd
        add     si, ax
        adc     cx, dx
; ---- slot 63 ----
        mov     ax, ss:[bx+126]
        xor     di, di
        test    ax, ax
        jle     V2DN0_63
        cmp     ax, 255
        jle     V2OK0_63
        mov     ax, 255
V2OK0_63:
        add     ax, ax
        mov     di, ax
V2DN0_63:
        mov     ax, [di+32256]
        cwd
        add     si, ax
        adc     cx, dx
        mov     ax, ss:[bp+126]
        xor     di, di
        test    ax, ax
        jle     V2DN1_63
        cmp     ax, 255
        jle     V2OK1_63
        mov     ax, 255
V2OK1_63:
        add     ax, ax
        mov     di, ax
V2DN1_63:
        mov     ax, [di+65024]
        cwd
        add     si, ax
        adc     cx, dx
; --- finish: (si:cx >> NNUE_SCALE_SHIFT) low word (score from stm's POV) ---
        mov     ax, cx
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        mov     dx, si
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        shr     dx, 1
        or      ax, dx
        pop     ds
        pop     di
        pop     si
        pop     bx
        pop     bp
        retf
nn_fwd_eval2_ ENDP

FWD_SHIFT_ADD:
        mov     al, ss:_nn_w2[di]
        cbw
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        cwd
        add     si, ax
        adc     cx, dx
        ret
FWD_SHIFT_SUB:
        mov     al, ss:_nn_w2[di]
        cbw
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        cwd
        sub     si, ax
        sbb     cx, dx
        ret

_TEXT   ENDS
        END
