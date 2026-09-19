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
        EXTRN   _nn_table:WORD
        EXTRN   _nn_w2:BYTE
        EXTRN   _nn_bias:WORD

        PUBLIC  nn_apply_add_
        PUBLIC  nn_apply_sub_
        PUBLIC  nn_make_move_
        PUBLIC  nn_make_cap_
        PUBLIC  nn_fwd_eval_
        PUBLIC  nn_fwd_eval8_
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

; =====================================================================
; nn_fwd_eval_ - NNUE forward pass (ReLU^2) via per-slot product tables:
; act = clamp(acc, 0, 255) and fwd[p][j][act] = (act^2 * w2) >>
; NNUE_ACT2_SHIFT (9) (the squared product is pre-shifted so every entry
; fits i16 - max 255^2*127>>9 = 16129). Every term is a plain table load.
; Table layout is the shared far array fwd[2][64][256]: fwd[0][j]
; base = j*512, fwd[1][j] base = 32768 + j*512 (persp 1 starts at +32768).
; si:cx = the 32-bit accumulator seeded from the bias; the finish shifts it
; right 5 (1.0 = 256 cp). Calling convention: FAR, ax=side.
; Preserves bx,bp,si,di,ds; clobbers ax,dx,cx,es.
; GENERATED by gen_nnue_batch.py - edit the script, not this block.
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
        mov     ax, SEG _nn_table
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
nn_fwd_eval_ ENDP

; =====================================================================

; =====================================================================
; nn_fwd_eval8_ - exact ReLU^2 forward for a selected narrow blob-v4
; material head. ax=side, dx=bucket. x32 weights are restricted to
; [-64,63], so one universal 64-KB signed table directly stores
; (activation^2*weight)>>8 for every neuron and bucket count.
; Calling convention: FAR; preserves bx,cx,bp,si,di,ds.
; GENERATED by gen_nnue_batch.py - edit the script, not this block.
nn_fwd_eval8_ PROC FAR
        push    bp
        push    bx
        push    si
        push    di
        push    ds
        push    cx
        push    ax
        mov     di, dx
        shl     di, 1
        shl     di, 1
        shl     di, 1
        shl     di, 1
        shl     di, 1
        shl     di, 1
        shl     di, 1
        add     di, OFFSET _nn_w2
        mov     bx, dx
        shl     bx, 1
        add     bx, OFFSET _nn_bias
        mov     ax, ss:[bx]
        cwd
        mov     si, ax
        mov     cx, dx
        pop     ax
        mov     bx, OFFSET _nn_acc
        mov     dx, ss:_nn_ply
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        add     bx, dx
        mov     bp, bx
        add     bp, 128
        mov     dx, ax
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        shl     dx, 1
        sub     bp, dx
        add     bx, dx
        mov     ax, SEG _nn_table
        mov     ds, ax
        push    bp
; ---- narrow bucketed S slot 0 ----
        mov     bp, ss:[bx+0]
        test    bp, bp
        jle     BNDNS_0
        cmp     bp, 255
        jle     BNOKS_0
        mov     bp, 255
BNOKS_0:
        mov     al, ss:[di+0]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_0:
; ---- narrow bucketed S slot 1 ----
        mov     bp, ss:[bx+2]
        test    bp, bp
        jle     BNDNS_1
        cmp     bp, 255
        jle     BNOKS_1
        mov     bp, 255
BNOKS_1:
        mov     al, ss:[di+1]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_1:
; ---- narrow bucketed S slot 2 ----
        mov     bp, ss:[bx+4]
        test    bp, bp
        jle     BNDNS_2
        cmp     bp, 255
        jle     BNOKS_2
        mov     bp, 255
BNOKS_2:
        mov     al, ss:[di+2]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_2:
; ---- narrow bucketed S slot 3 ----
        mov     bp, ss:[bx+6]
        test    bp, bp
        jle     BNDNS_3
        cmp     bp, 255
        jle     BNOKS_3
        mov     bp, 255
BNOKS_3:
        mov     al, ss:[di+3]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_3:
; ---- narrow bucketed S slot 4 ----
        mov     bp, ss:[bx+8]
        test    bp, bp
        jle     BNDNS_4
        cmp     bp, 255
        jle     BNOKS_4
        mov     bp, 255
BNOKS_4:
        mov     al, ss:[di+4]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_4:
; ---- narrow bucketed S slot 5 ----
        mov     bp, ss:[bx+10]
        test    bp, bp
        jle     BNDNS_5
        cmp     bp, 255
        jle     BNOKS_5
        mov     bp, 255
BNOKS_5:
        mov     al, ss:[di+5]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_5:
; ---- narrow bucketed S slot 6 ----
        mov     bp, ss:[bx+12]
        test    bp, bp
        jle     BNDNS_6
        cmp     bp, 255
        jle     BNOKS_6
        mov     bp, 255
BNOKS_6:
        mov     al, ss:[di+6]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_6:
; ---- narrow bucketed S slot 7 ----
        mov     bp, ss:[bx+14]
        test    bp, bp
        jle     BNDNS_7
        cmp     bp, 255
        jle     BNOKS_7
        mov     bp, 255
BNOKS_7:
        mov     al, ss:[di+7]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_7:
; ---- narrow bucketed S slot 8 ----
        mov     bp, ss:[bx+16]
        test    bp, bp
        jle     BNDNS_8
        cmp     bp, 255
        jle     BNOKS_8
        mov     bp, 255
BNOKS_8:
        mov     al, ss:[di+8]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_8:
; ---- narrow bucketed S slot 9 ----
        mov     bp, ss:[bx+18]
        test    bp, bp
        jle     BNDNS_9
        cmp     bp, 255
        jle     BNOKS_9
        mov     bp, 255
BNOKS_9:
        mov     al, ss:[di+9]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_9:
; ---- narrow bucketed S slot 10 ----
        mov     bp, ss:[bx+20]
        test    bp, bp
        jle     BNDNS_10
        cmp     bp, 255
        jle     BNOKS_10
        mov     bp, 255
BNOKS_10:
        mov     al, ss:[di+10]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_10:
; ---- narrow bucketed S slot 11 ----
        mov     bp, ss:[bx+22]
        test    bp, bp
        jle     BNDNS_11
        cmp     bp, 255
        jle     BNOKS_11
        mov     bp, 255
BNOKS_11:
        mov     al, ss:[di+11]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_11:
; ---- narrow bucketed S slot 12 ----
        mov     bp, ss:[bx+24]
        test    bp, bp
        jle     BNDNS_12
        cmp     bp, 255
        jle     BNOKS_12
        mov     bp, 255
BNOKS_12:
        mov     al, ss:[di+12]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_12:
; ---- narrow bucketed S slot 13 ----
        mov     bp, ss:[bx+26]
        test    bp, bp
        jle     BNDNS_13
        cmp     bp, 255
        jle     BNOKS_13
        mov     bp, 255
BNOKS_13:
        mov     al, ss:[di+13]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_13:
; ---- narrow bucketed S slot 14 ----
        mov     bp, ss:[bx+28]
        test    bp, bp
        jle     BNDNS_14
        cmp     bp, 255
        jle     BNOKS_14
        mov     bp, 255
BNOKS_14:
        mov     al, ss:[di+14]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_14:
; ---- narrow bucketed S slot 15 ----
        mov     bp, ss:[bx+30]
        test    bp, bp
        jle     BNDNS_15
        cmp     bp, 255
        jle     BNOKS_15
        mov     bp, 255
BNOKS_15:
        mov     al, ss:[di+15]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_15:
; ---- narrow bucketed S slot 16 ----
        mov     bp, ss:[bx+32]
        test    bp, bp
        jle     BNDNS_16
        cmp     bp, 255
        jle     BNOKS_16
        mov     bp, 255
BNOKS_16:
        mov     al, ss:[di+16]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_16:
; ---- narrow bucketed S slot 17 ----
        mov     bp, ss:[bx+34]
        test    bp, bp
        jle     BNDNS_17
        cmp     bp, 255
        jle     BNOKS_17
        mov     bp, 255
BNOKS_17:
        mov     al, ss:[di+17]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_17:
; ---- narrow bucketed S slot 18 ----
        mov     bp, ss:[bx+36]
        test    bp, bp
        jle     BNDNS_18
        cmp     bp, 255
        jle     BNOKS_18
        mov     bp, 255
BNOKS_18:
        mov     al, ss:[di+18]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_18:
; ---- narrow bucketed S slot 19 ----
        mov     bp, ss:[bx+38]
        test    bp, bp
        jle     BNDNS_19
        cmp     bp, 255
        jle     BNOKS_19
        mov     bp, 255
BNOKS_19:
        mov     al, ss:[di+19]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_19:
; ---- narrow bucketed S slot 20 ----
        mov     bp, ss:[bx+40]
        test    bp, bp
        jle     BNDNS_20
        cmp     bp, 255
        jle     BNOKS_20
        mov     bp, 255
BNOKS_20:
        mov     al, ss:[di+20]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_20:
; ---- narrow bucketed S slot 21 ----
        mov     bp, ss:[bx+42]
        test    bp, bp
        jle     BNDNS_21
        cmp     bp, 255
        jle     BNOKS_21
        mov     bp, 255
BNOKS_21:
        mov     al, ss:[di+21]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_21:
; ---- narrow bucketed S slot 22 ----
        mov     bp, ss:[bx+44]
        test    bp, bp
        jle     BNDNS_22
        cmp     bp, 255
        jle     BNOKS_22
        mov     bp, 255
BNOKS_22:
        mov     al, ss:[di+22]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_22:
; ---- narrow bucketed S slot 23 ----
        mov     bp, ss:[bx+46]
        test    bp, bp
        jle     BNDNS_23
        cmp     bp, 255
        jle     BNOKS_23
        mov     bp, 255
BNOKS_23:
        mov     al, ss:[di+23]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_23:
; ---- narrow bucketed S slot 24 ----
        mov     bp, ss:[bx+48]
        test    bp, bp
        jle     BNDNS_24
        cmp     bp, 255
        jle     BNOKS_24
        mov     bp, 255
BNOKS_24:
        mov     al, ss:[di+24]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_24:
; ---- narrow bucketed S slot 25 ----
        mov     bp, ss:[bx+50]
        test    bp, bp
        jle     BNDNS_25
        cmp     bp, 255
        jle     BNOKS_25
        mov     bp, 255
BNOKS_25:
        mov     al, ss:[di+25]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_25:
; ---- narrow bucketed S slot 26 ----
        mov     bp, ss:[bx+52]
        test    bp, bp
        jle     BNDNS_26
        cmp     bp, 255
        jle     BNOKS_26
        mov     bp, 255
BNOKS_26:
        mov     al, ss:[di+26]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_26:
; ---- narrow bucketed S slot 27 ----
        mov     bp, ss:[bx+54]
        test    bp, bp
        jle     BNDNS_27
        cmp     bp, 255
        jle     BNOKS_27
        mov     bp, 255
BNOKS_27:
        mov     al, ss:[di+27]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_27:
; ---- narrow bucketed S slot 28 ----
        mov     bp, ss:[bx+56]
        test    bp, bp
        jle     BNDNS_28
        cmp     bp, 255
        jle     BNOKS_28
        mov     bp, 255
BNOKS_28:
        mov     al, ss:[di+28]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_28:
; ---- narrow bucketed S slot 29 ----
        mov     bp, ss:[bx+58]
        test    bp, bp
        jle     BNDNS_29
        cmp     bp, 255
        jle     BNOKS_29
        mov     bp, 255
BNOKS_29:
        mov     al, ss:[di+29]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_29:
; ---- narrow bucketed S slot 30 ----
        mov     bp, ss:[bx+60]
        test    bp, bp
        jle     BNDNS_30
        cmp     bp, 255
        jle     BNOKS_30
        mov     bp, 255
BNOKS_30:
        mov     al, ss:[di+30]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_30:
; ---- narrow bucketed S slot 31 ----
        mov     bp, ss:[bx+62]
        test    bp, bp
        jle     BNDNS_31
        cmp     bp, 255
        jle     BNOKS_31
        mov     bp, 255
BNOKS_31:
        mov     al, ss:[di+31]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_31:
; ---- narrow bucketed S slot 32 ----
        mov     bp, ss:[bx+64]
        test    bp, bp
        jle     BNDNS_32
        cmp     bp, 255
        jle     BNOKS_32
        mov     bp, 255
BNOKS_32:
        mov     al, ss:[di+32]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_32:
; ---- narrow bucketed S slot 33 ----
        mov     bp, ss:[bx+66]
        test    bp, bp
        jle     BNDNS_33
        cmp     bp, 255
        jle     BNOKS_33
        mov     bp, 255
BNOKS_33:
        mov     al, ss:[di+33]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_33:
; ---- narrow bucketed S slot 34 ----
        mov     bp, ss:[bx+68]
        test    bp, bp
        jle     BNDNS_34
        cmp     bp, 255
        jle     BNOKS_34
        mov     bp, 255
BNOKS_34:
        mov     al, ss:[di+34]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_34:
; ---- narrow bucketed S slot 35 ----
        mov     bp, ss:[bx+70]
        test    bp, bp
        jle     BNDNS_35
        cmp     bp, 255
        jle     BNOKS_35
        mov     bp, 255
BNOKS_35:
        mov     al, ss:[di+35]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_35:
; ---- narrow bucketed S slot 36 ----
        mov     bp, ss:[bx+72]
        test    bp, bp
        jle     BNDNS_36
        cmp     bp, 255
        jle     BNOKS_36
        mov     bp, 255
BNOKS_36:
        mov     al, ss:[di+36]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_36:
; ---- narrow bucketed S slot 37 ----
        mov     bp, ss:[bx+74]
        test    bp, bp
        jle     BNDNS_37
        cmp     bp, 255
        jle     BNOKS_37
        mov     bp, 255
BNOKS_37:
        mov     al, ss:[di+37]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_37:
; ---- narrow bucketed S slot 38 ----
        mov     bp, ss:[bx+76]
        test    bp, bp
        jle     BNDNS_38
        cmp     bp, 255
        jle     BNOKS_38
        mov     bp, 255
BNOKS_38:
        mov     al, ss:[di+38]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_38:
; ---- narrow bucketed S slot 39 ----
        mov     bp, ss:[bx+78]
        test    bp, bp
        jle     BNDNS_39
        cmp     bp, 255
        jle     BNOKS_39
        mov     bp, 255
BNOKS_39:
        mov     al, ss:[di+39]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_39:
; ---- narrow bucketed S slot 40 ----
        mov     bp, ss:[bx+80]
        test    bp, bp
        jle     BNDNS_40
        cmp     bp, 255
        jle     BNOKS_40
        mov     bp, 255
BNOKS_40:
        mov     al, ss:[di+40]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_40:
; ---- narrow bucketed S slot 41 ----
        mov     bp, ss:[bx+82]
        test    bp, bp
        jle     BNDNS_41
        cmp     bp, 255
        jle     BNOKS_41
        mov     bp, 255
BNOKS_41:
        mov     al, ss:[di+41]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_41:
; ---- narrow bucketed S slot 42 ----
        mov     bp, ss:[bx+84]
        test    bp, bp
        jle     BNDNS_42
        cmp     bp, 255
        jle     BNOKS_42
        mov     bp, 255
BNOKS_42:
        mov     al, ss:[di+42]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_42:
; ---- narrow bucketed S slot 43 ----
        mov     bp, ss:[bx+86]
        test    bp, bp
        jle     BNDNS_43
        cmp     bp, 255
        jle     BNOKS_43
        mov     bp, 255
BNOKS_43:
        mov     al, ss:[di+43]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_43:
; ---- narrow bucketed S slot 44 ----
        mov     bp, ss:[bx+88]
        test    bp, bp
        jle     BNDNS_44
        cmp     bp, 255
        jle     BNOKS_44
        mov     bp, 255
BNOKS_44:
        mov     al, ss:[di+44]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_44:
; ---- narrow bucketed S slot 45 ----
        mov     bp, ss:[bx+90]
        test    bp, bp
        jle     BNDNS_45
        cmp     bp, 255
        jle     BNOKS_45
        mov     bp, 255
BNOKS_45:
        mov     al, ss:[di+45]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_45:
; ---- narrow bucketed S slot 46 ----
        mov     bp, ss:[bx+92]
        test    bp, bp
        jle     BNDNS_46
        cmp     bp, 255
        jle     BNOKS_46
        mov     bp, 255
BNOKS_46:
        mov     al, ss:[di+46]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_46:
; ---- narrow bucketed S slot 47 ----
        mov     bp, ss:[bx+94]
        test    bp, bp
        jle     BNDNS_47
        cmp     bp, 255
        jle     BNOKS_47
        mov     bp, 255
BNOKS_47:
        mov     al, ss:[di+47]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_47:
; ---- narrow bucketed S slot 48 ----
        mov     bp, ss:[bx+96]
        test    bp, bp
        jle     BNDNS_48
        cmp     bp, 255
        jle     BNOKS_48
        mov     bp, 255
BNOKS_48:
        mov     al, ss:[di+48]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_48:
; ---- narrow bucketed S slot 49 ----
        mov     bp, ss:[bx+98]
        test    bp, bp
        jle     BNDNS_49
        cmp     bp, 255
        jle     BNOKS_49
        mov     bp, 255
BNOKS_49:
        mov     al, ss:[di+49]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_49:
; ---- narrow bucketed S slot 50 ----
        mov     bp, ss:[bx+100]
        test    bp, bp
        jle     BNDNS_50
        cmp     bp, 255
        jle     BNOKS_50
        mov     bp, 255
BNOKS_50:
        mov     al, ss:[di+50]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_50:
; ---- narrow bucketed S slot 51 ----
        mov     bp, ss:[bx+102]
        test    bp, bp
        jle     BNDNS_51
        cmp     bp, 255
        jle     BNOKS_51
        mov     bp, 255
BNOKS_51:
        mov     al, ss:[di+51]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_51:
; ---- narrow bucketed S slot 52 ----
        mov     bp, ss:[bx+104]
        test    bp, bp
        jle     BNDNS_52
        cmp     bp, 255
        jle     BNOKS_52
        mov     bp, 255
BNOKS_52:
        mov     al, ss:[di+52]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_52:
; ---- narrow bucketed S slot 53 ----
        mov     bp, ss:[bx+106]
        test    bp, bp
        jle     BNDNS_53
        cmp     bp, 255
        jle     BNOKS_53
        mov     bp, 255
BNOKS_53:
        mov     al, ss:[di+53]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_53:
; ---- narrow bucketed S slot 54 ----
        mov     bp, ss:[bx+108]
        test    bp, bp
        jle     BNDNS_54
        cmp     bp, 255
        jle     BNOKS_54
        mov     bp, 255
BNOKS_54:
        mov     al, ss:[di+54]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_54:
; ---- narrow bucketed S slot 55 ----
        mov     bp, ss:[bx+110]
        test    bp, bp
        jle     BNDNS_55
        cmp     bp, 255
        jle     BNOKS_55
        mov     bp, 255
BNOKS_55:
        mov     al, ss:[di+55]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_55:
; ---- narrow bucketed S slot 56 ----
        mov     bp, ss:[bx+112]
        test    bp, bp
        jle     BNDNS_56
        cmp     bp, 255
        jle     BNOKS_56
        mov     bp, 255
BNOKS_56:
        mov     al, ss:[di+56]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_56:
; ---- narrow bucketed S slot 57 ----
        mov     bp, ss:[bx+114]
        test    bp, bp
        jle     BNDNS_57
        cmp     bp, 255
        jle     BNOKS_57
        mov     bp, 255
BNOKS_57:
        mov     al, ss:[di+57]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_57:
; ---- narrow bucketed S slot 58 ----
        mov     bp, ss:[bx+116]
        test    bp, bp
        jle     BNDNS_58
        cmp     bp, 255
        jle     BNOKS_58
        mov     bp, 255
BNOKS_58:
        mov     al, ss:[di+58]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_58:
; ---- narrow bucketed S slot 59 ----
        mov     bp, ss:[bx+118]
        test    bp, bp
        jle     BNDNS_59
        cmp     bp, 255
        jle     BNOKS_59
        mov     bp, 255
BNOKS_59:
        mov     al, ss:[di+59]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_59:
; ---- narrow bucketed S slot 60 ----
        mov     bp, ss:[bx+120]
        test    bp, bp
        jle     BNDNS_60
        cmp     bp, 255
        jle     BNOKS_60
        mov     bp, 255
BNOKS_60:
        mov     al, ss:[di+60]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_60:
; ---- narrow bucketed S slot 61 ----
        mov     bp, ss:[bx+122]
        test    bp, bp
        jle     BNDNS_61
        cmp     bp, 255
        jle     BNOKS_61
        mov     bp, 255
BNOKS_61:
        mov     al, ss:[di+61]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_61:
; ---- narrow bucketed S slot 62 ----
        mov     bp, ss:[bx+124]
        test    bp, bp
        jle     BNDNS_62
        cmp     bp, 255
        jle     BNOKS_62
        mov     bp, 255
BNOKS_62:
        mov     al, ss:[di+62]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_62:
; ---- narrow bucketed S slot 63 ----
        mov     bp, ss:[bx+126]
        test    bp, bp
        jle     BNDNS_63
        cmp     bp, 255
        jle     BNOKS_63
        mov     bp, 255
BNOKS_63:
        mov     al, ss:[di+63]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNS_63:
; --- switch from stm to nstm accumulator; second weight row is +64 ---
        pop     bx
; ---- narrow bucketed N slot 0 ----
        mov     bp, ss:[bx+0]
        test    bp, bp
        jle     BNDNN_0
        cmp     bp, 255
        jle     BNOKN_0
        mov     bp, 255
BNOKN_0:
        mov     al, ss:[di+64]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_0:
; ---- narrow bucketed N slot 1 ----
        mov     bp, ss:[bx+2]
        test    bp, bp
        jle     BNDNN_1
        cmp     bp, 255
        jle     BNOKN_1
        mov     bp, 255
BNOKN_1:
        mov     al, ss:[di+65]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_1:
; ---- narrow bucketed N slot 2 ----
        mov     bp, ss:[bx+4]
        test    bp, bp
        jle     BNDNN_2
        cmp     bp, 255
        jle     BNOKN_2
        mov     bp, 255
BNOKN_2:
        mov     al, ss:[di+66]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_2:
; ---- narrow bucketed N slot 3 ----
        mov     bp, ss:[bx+6]
        test    bp, bp
        jle     BNDNN_3
        cmp     bp, 255
        jle     BNOKN_3
        mov     bp, 255
BNOKN_3:
        mov     al, ss:[di+67]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_3:
; ---- narrow bucketed N slot 4 ----
        mov     bp, ss:[bx+8]
        test    bp, bp
        jle     BNDNN_4
        cmp     bp, 255
        jle     BNOKN_4
        mov     bp, 255
BNOKN_4:
        mov     al, ss:[di+68]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_4:
; ---- narrow bucketed N slot 5 ----
        mov     bp, ss:[bx+10]
        test    bp, bp
        jle     BNDNN_5
        cmp     bp, 255
        jle     BNOKN_5
        mov     bp, 255
BNOKN_5:
        mov     al, ss:[di+69]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_5:
; ---- narrow bucketed N slot 6 ----
        mov     bp, ss:[bx+12]
        test    bp, bp
        jle     BNDNN_6
        cmp     bp, 255
        jle     BNOKN_6
        mov     bp, 255
BNOKN_6:
        mov     al, ss:[di+70]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_6:
; ---- narrow bucketed N slot 7 ----
        mov     bp, ss:[bx+14]
        test    bp, bp
        jle     BNDNN_7
        cmp     bp, 255
        jle     BNOKN_7
        mov     bp, 255
BNOKN_7:
        mov     al, ss:[di+71]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_7:
; ---- narrow bucketed N slot 8 ----
        mov     bp, ss:[bx+16]
        test    bp, bp
        jle     BNDNN_8
        cmp     bp, 255
        jle     BNOKN_8
        mov     bp, 255
BNOKN_8:
        mov     al, ss:[di+72]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_8:
; ---- narrow bucketed N slot 9 ----
        mov     bp, ss:[bx+18]
        test    bp, bp
        jle     BNDNN_9
        cmp     bp, 255
        jle     BNOKN_9
        mov     bp, 255
BNOKN_9:
        mov     al, ss:[di+73]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_9:
; ---- narrow bucketed N slot 10 ----
        mov     bp, ss:[bx+20]
        test    bp, bp
        jle     BNDNN_10
        cmp     bp, 255
        jle     BNOKN_10
        mov     bp, 255
BNOKN_10:
        mov     al, ss:[di+74]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_10:
; ---- narrow bucketed N slot 11 ----
        mov     bp, ss:[bx+22]
        test    bp, bp
        jle     BNDNN_11
        cmp     bp, 255
        jle     BNOKN_11
        mov     bp, 255
BNOKN_11:
        mov     al, ss:[di+75]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_11:
; ---- narrow bucketed N slot 12 ----
        mov     bp, ss:[bx+24]
        test    bp, bp
        jle     BNDNN_12
        cmp     bp, 255
        jle     BNOKN_12
        mov     bp, 255
BNOKN_12:
        mov     al, ss:[di+76]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_12:
; ---- narrow bucketed N slot 13 ----
        mov     bp, ss:[bx+26]
        test    bp, bp
        jle     BNDNN_13
        cmp     bp, 255
        jle     BNOKN_13
        mov     bp, 255
BNOKN_13:
        mov     al, ss:[di+77]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_13:
; ---- narrow bucketed N slot 14 ----
        mov     bp, ss:[bx+28]
        test    bp, bp
        jle     BNDNN_14
        cmp     bp, 255
        jle     BNOKN_14
        mov     bp, 255
BNOKN_14:
        mov     al, ss:[di+78]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_14:
; ---- narrow bucketed N slot 15 ----
        mov     bp, ss:[bx+30]
        test    bp, bp
        jle     BNDNN_15
        cmp     bp, 255
        jle     BNOKN_15
        mov     bp, 255
BNOKN_15:
        mov     al, ss:[di+79]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_15:
; ---- narrow bucketed N slot 16 ----
        mov     bp, ss:[bx+32]
        test    bp, bp
        jle     BNDNN_16
        cmp     bp, 255
        jle     BNOKN_16
        mov     bp, 255
BNOKN_16:
        mov     al, ss:[di+80]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_16:
; ---- narrow bucketed N slot 17 ----
        mov     bp, ss:[bx+34]
        test    bp, bp
        jle     BNDNN_17
        cmp     bp, 255
        jle     BNOKN_17
        mov     bp, 255
BNOKN_17:
        mov     al, ss:[di+81]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_17:
; ---- narrow bucketed N slot 18 ----
        mov     bp, ss:[bx+36]
        test    bp, bp
        jle     BNDNN_18
        cmp     bp, 255
        jle     BNOKN_18
        mov     bp, 255
BNOKN_18:
        mov     al, ss:[di+82]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_18:
; ---- narrow bucketed N slot 19 ----
        mov     bp, ss:[bx+38]
        test    bp, bp
        jle     BNDNN_19
        cmp     bp, 255
        jle     BNOKN_19
        mov     bp, 255
BNOKN_19:
        mov     al, ss:[di+83]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_19:
; ---- narrow bucketed N slot 20 ----
        mov     bp, ss:[bx+40]
        test    bp, bp
        jle     BNDNN_20
        cmp     bp, 255
        jle     BNOKN_20
        mov     bp, 255
BNOKN_20:
        mov     al, ss:[di+84]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_20:
; ---- narrow bucketed N slot 21 ----
        mov     bp, ss:[bx+42]
        test    bp, bp
        jle     BNDNN_21
        cmp     bp, 255
        jle     BNOKN_21
        mov     bp, 255
BNOKN_21:
        mov     al, ss:[di+85]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_21:
; ---- narrow bucketed N slot 22 ----
        mov     bp, ss:[bx+44]
        test    bp, bp
        jle     BNDNN_22
        cmp     bp, 255
        jle     BNOKN_22
        mov     bp, 255
BNOKN_22:
        mov     al, ss:[di+86]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_22:
; ---- narrow bucketed N slot 23 ----
        mov     bp, ss:[bx+46]
        test    bp, bp
        jle     BNDNN_23
        cmp     bp, 255
        jle     BNOKN_23
        mov     bp, 255
BNOKN_23:
        mov     al, ss:[di+87]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_23:
; ---- narrow bucketed N slot 24 ----
        mov     bp, ss:[bx+48]
        test    bp, bp
        jle     BNDNN_24
        cmp     bp, 255
        jle     BNOKN_24
        mov     bp, 255
BNOKN_24:
        mov     al, ss:[di+88]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_24:
; ---- narrow bucketed N slot 25 ----
        mov     bp, ss:[bx+50]
        test    bp, bp
        jle     BNDNN_25
        cmp     bp, 255
        jle     BNOKN_25
        mov     bp, 255
BNOKN_25:
        mov     al, ss:[di+89]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_25:
; ---- narrow bucketed N slot 26 ----
        mov     bp, ss:[bx+52]
        test    bp, bp
        jle     BNDNN_26
        cmp     bp, 255
        jle     BNOKN_26
        mov     bp, 255
BNOKN_26:
        mov     al, ss:[di+90]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_26:
; ---- narrow bucketed N slot 27 ----
        mov     bp, ss:[bx+54]
        test    bp, bp
        jle     BNDNN_27
        cmp     bp, 255
        jle     BNOKN_27
        mov     bp, 255
BNOKN_27:
        mov     al, ss:[di+91]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_27:
; ---- narrow bucketed N slot 28 ----
        mov     bp, ss:[bx+56]
        test    bp, bp
        jle     BNDNN_28
        cmp     bp, 255
        jle     BNOKN_28
        mov     bp, 255
BNOKN_28:
        mov     al, ss:[di+92]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_28:
; ---- narrow bucketed N slot 29 ----
        mov     bp, ss:[bx+58]
        test    bp, bp
        jle     BNDNN_29
        cmp     bp, 255
        jle     BNOKN_29
        mov     bp, 255
BNOKN_29:
        mov     al, ss:[di+93]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_29:
; ---- narrow bucketed N slot 30 ----
        mov     bp, ss:[bx+60]
        test    bp, bp
        jle     BNDNN_30
        cmp     bp, 255
        jle     BNOKN_30
        mov     bp, 255
BNOKN_30:
        mov     al, ss:[di+94]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_30:
; ---- narrow bucketed N slot 31 ----
        mov     bp, ss:[bx+62]
        test    bp, bp
        jle     BNDNN_31
        cmp     bp, 255
        jle     BNOKN_31
        mov     bp, 255
BNOKN_31:
        mov     al, ss:[di+95]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_31:
; ---- narrow bucketed N slot 32 ----
        mov     bp, ss:[bx+64]
        test    bp, bp
        jle     BNDNN_32
        cmp     bp, 255
        jle     BNOKN_32
        mov     bp, 255
BNOKN_32:
        mov     al, ss:[di+96]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_32:
; ---- narrow bucketed N slot 33 ----
        mov     bp, ss:[bx+66]
        test    bp, bp
        jle     BNDNN_33
        cmp     bp, 255
        jle     BNOKN_33
        mov     bp, 255
BNOKN_33:
        mov     al, ss:[di+97]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_33:
; ---- narrow bucketed N slot 34 ----
        mov     bp, ss:[bx+68]
        test    bp, bp
        jle     BNDNN_34
        cmp     bp, 255
        jle     BNOKN_34
        mov     bp, 255
BNOKN_34:
        mov     al, ss:[di+98]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_34:
; ---- narrow bucketed N slot 35 ----
        mov     bp, ss:[bx+70]
        test    bp, bp
        jle     BNDNN_35
        cmp     bp, 255
        jle     BNOKN_35
        mov     bp, 255
BNOKN_35:
        mov     al, ss:[di+99]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_35:
; ---- narrow bucketed N slot 36 ----
        mov     bp, ss:[bx+72]
        test    bp, bp
        jle     BNDNN_36
        cmp     bp, 255
        jle     BNOKN_36
        mov     bp, 255
BNOKN_36:
        mov     al, ss:[di+100]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_36:
; ---- narrow bucketed N slot 37 ----
        mov     bp, ss:[bx+74]
        test    bp, bp
        jle     BNDNN_37
        cmp     bp, 255
        jle     BNOKN_37
        mov     bp, 255
BNOKN_37:
        mov     al, ss:[di+101]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_37:
; ---- narrow bucketed N slot 38 ----
        mov     bp, ss:[bx+76]
        test    bp, bp
        jle     BNDNN_38
        cmp     bp, 255
        jle     BNOKN_38
        mov     bp, 255
BNOKN_38:
        mov     al, ss:[di+102]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_38:
; ---- narrow bucketed N slot 39 ----
        mov     bp, ss:[bx+78]
        test    bp, bp
        jle     BNDNN_39
        cmp     bp, 255
        jle     BNOKN_39
        mov     bp, 255
BNOKN_39:
        mov     al, ss:[di+103]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_39:
; ---- narrow bucketed N slot 40 ----
        mov     bp, ss:[bx+80]
        test    bp, bp
        jle     BNDNN_40
        cmp     bp, 255
        jle     BNOKN_40
        mov     bp, 255
BNOKN_40:
        mov     al, ss:[di+104]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_40:
; ---- narrow bucketed N slot 41 ----
        mov     bp, ss:[bx+82]
        test    bp, bp
        jle     BNDNN_41
        cmp     bp, 255
        jle     BNOKN_41
        mov     bp, 255
BNOKN_41:
        mov     al, ss:[di+105]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_41:
; ---- narrow bucketed N slot 42 ----
        mov     bp, ss:[bx+84]
        test    bp, bp
        jle     BNDNN_42
        cmp     bp, 255
        jle     BNOKN_42
        mov     bp, 255
BNOKN_42:
        mov     al, ss:[di+106]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_42:
; ---- narrow bucketed N slot 43 ----
        mov     bp, ss:[bx+86]
        test    bp, bp
        jle     BNDNN_43
        cmp     bp, 255
        jle     BNOKN_43
        mov     bp, 255
BNOKN_43:
        mov     al, ss:[di+107]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_43:
; ---- narrow bucketed N slot 44 ----
        mov     bp, ss:[bx+88]
        test    bp, bp
        jle     BNDNN_44
        cmp     bp, 255
        jle     BNOKN_44
        mov     bp, 255
BNOKN_44:
        mov     al, ss:[di+108]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_44:
; ---- narrow bucketed N slot 45 ----
        mov     bp, ss:[bx+90]
        test    bp, bp
        jle     BNDNN_45
        cmp     bp, 255
        jle     BNOKN_45
        mov     bp, 255
BNOKN_45:
        mov     al, ss:[di+109]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_45:
; ---- narrow bucketed N slot 46 ----
        mov     bp, ss:[bx+92]
        test    bp, bp
        jle     BNDNN_46
        cmp     bp, 255
        jle     BNOKN_46
        mov     bp, 255
BNOKN_46:
        mov     al, ss:[di+110]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_46:
; ---- narrow bucketed N slot 47 ----
        mov     bp, ss:[bx+94]
        test    bp, bp
        jle     BNDNN_47
        cmp     bp, 255
        jle     BNOKN_47
        mov     bp, 255
BNOKN_47:
        mov     al, ss:[di+111]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_47:
; ---- narrow bucketed N slot 48 ----
        mov     bp, ss:[bx+96]
        test    bp, bp
        jle     BNDNN_48
        cmp     bp, 255
        jle     BNOKN_48
        mov     bp, 255
BNOKN_48:
        mov     al, ss:[di+112]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_48:
; ---- narrow bucketed N slot 49 ----
        mov     bp, ss:[bx+98]
        test    bp, bp
        jle     BNDNN_49
        cmp     bp, 255
        jle     BNOKN_49
        mov     bp, 255
BNOKN_49:
        mov     al, ss:[di+113]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_49:
; ---- narrow bucketed N slot 50 ----
        mov     bp, ss:[bx+100]
        test    bp, bp
        jle     BNDNN_50
        cmp     bp, 255
        jle     BNOKN_50
        mov     bp, 255
BNOKN_50:
        mov     al, ss:[di+114]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_50:
; ---- narrow bucketed N slot 51 ----
        mov     bp, ss:[bx+102]
        test    bp, bp
        jle     BNDNN_51
        cmp     bp, 255
        jle     BNOKN_51
        mov     bp, 255
BNOKN_51:
        mov     al, ss:[di+115]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_51:
; ---- narrow bucketed N slot 52 ----
        mov     bp, ss:[bx+104]
        test    bp, bp
        jle     BNDNN_52
        cmp     bp, 255
        jle     BNOKN_52
        mov     bp, 255
BNOKN_52:
        mov     al, ss:[di+116]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_52:
; ---- narrow bucketed N slot 53 ----
        mov     bp, ss:[bx+106]
        test    bp, bp
        jle     BNDNN_53
        cmp     bp, 255
        jle     BNOKN_53
        mov     bp, 255
BNOKN_53:
        mov     al, ss:[di+117]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_53:
; ---- narrow bucketed N slot 54 ----
        mov     bp, ss:[bx+108]
        test    bp, bp
        jle     BNDNN_54
        cmp     bp, 255
        jle     BNOKN_54
        mov     bp, 255
BNOKN_54:
        mov     al, ss:[di+118]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_54:
; ---- narrow bucketed N slot 55 ----
        mov     bp, ss:[bx+110]
        test    bp, bp
        jle     BNDNN_55
        cmp     bp, 255
        jle     BNOKN_55
        mov     bp, 255
BNOKN_55:
        mov     al, ss:[di+119]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_55:
; ---- narrow bucketed N slot 56 ----
        mov     bp, ss:[bx+112]
        test    bp, bp
        jle     BNDNN_56
        cmp     bp, 255
        jle     BNOKN_56
        mov     bp, 255
BNOKN_56:
        mov     al, ss:[di+120]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_56:
; ---- narrow bucketed N slot 57 ----
        mov     bp, ss:[bx+114]
        test    bp, bp
        jle     BNDNN_57
        cmp     bp, 255
        jle     BNOKN_57
        mov     bp, 255
BNOKN_57:
        mov     al, ss:[di+121]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_57:
; ---- narrow bucketed N slot 58 ----
        mov     bp, ss:[bx+116]
        test    bp, bp
        jle     BNDNN_58
        cmp     bp, 255
        jle     BNOKN_58
        mov     bp, 255
BNOKN_58:
        mov     al, ss:[di+122]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_58:
; ---- narrow bucketed N slot 59 ----
        mov     bp, ss:[bx+118]
        test    bp, bp
        jle     BNDNN_59
        cmp     bp, 255
        jle     BNOKN_59
        mov     bp, 255
BNOKN_59:
        mov     al, ss:[di+123]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_59:
; ---- narrow bucketed N slot 60 ----
        mov     bp, ss:[bx+120]
        test    bp, bp
        jle     BNDNN_60
        cmp     bp, 255
        jle     BNOKN_60
        mov     bp, 255
BNOKN_60:
        mov     al, ss:[di+124]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_60:
; ---- narrow bucketed N slot 61 ----
        mov     bp, ss:[bx+122]
        test    bp, bp
        jle     BNDNN_61
        cmp     bp, 255
        jle     BNOKN_61
        mov     bp, 255
BNOKN_61:
        mov     al, ss:[di+125]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_61:
; ---- narrow bucketed N slot 62 ----
        mov     bp, ss:[bx+124]
        test    bp, bp
        jle     BNDNN_62
        cmp     bp, 255
        jle     BNOKN_62
        mov     bp, 255
BNOKN_62:
        mov     al, ss:[di+126]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_62:
; ---- narrow bucketed N slot 63 ----
        mov     bp, ss:[bx+126]
        test    bp, bp
        jle     BNDNN_63
        cmp     bp, 255
        jle     BNOKN_63
        mov     bp, 255
BNOKN_63:
        mov     al, ss:[di+127]
        add     al, 64
        mov     ah, al
        xor     al, al
        shl     ax, 1
        mov     dx, bp
        shl     dx, 1
        add     ax, dx
        mov     bp, ax
        mov     ax, ds:[bp]
        cwd
        add     si, ax
        adc     cx, dx
BNDNN_63:
; --- finish: (si:cx >> NNUE_SCALE_SHIFT) low word ---
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
        pop     cx
        pop     ds
        pop     di
        pop     si
        pop     bx
        pop     bp
        retf
nn_fwd_eval8_ ENDP

_TEXT   ENDS
        END
