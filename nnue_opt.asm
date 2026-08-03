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
        EXTRN   _nn_fwd0:WORD
        EXTRN   _nn_fwd1:WORD
        EXTRN   _nn_w2:BYTE
        EXTRN   _nn_bias:WORD

        PUBLIC  nn_apply_add_
        PUBLIC  nn_apply_sub_
        PUBLIC  nn_fwd_eval_
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
; nn_fwd_eval_ - NNUE forward pass via per-slot product tables.
; int nn_fwd_eval(int side);  ax = side (0 white, 1 black); returns eval in ax.
; Reads ss:_nn_acc (2x64 i16), ss:_nn_w2 (128 i8), ss:_nn_bias (i16), and the
; far product tables _nn_fwd0/_nn_fwd1 ([64][256] i16; entry [j][a+128] =
; w2[p*64+j]*a). IMPORTANT: Watcom places both _far arrays in ONE far segment
; (nnue13_DATA, 64 KB), _nn_fwd0 at offset 0 and _nn_fwd1 at offset 32768, so
; persp1 offsets carry +32768 and only one segment register is needed.
; Fast path: |a|<128 -> index=2*a, load the i16 product directly. The +/-128
; clamp extremes take the (rare) shift handlers w2[bx]<<7. SI:CX is the i32
; accumulator; result is (acc >> NNUE_SCALE_SHIFT) and negated when the side to
; move is black. Preserves bx,si,di,bp,ds; clobbers ax,dx,cx,es.
nn_fwd_eval_ PROC FAR
        push    bp
        push    bx
        push    si
        push    di
        push    ds
        mov     bp, ax                  ; bp = side
        mov     ax, SEG _nn_fwd0
        mov     ds, ax                  ; ds = shared fwd0/fwd1 segment
        mov     ax, ss:_nn_bias
        cwd
        mov     si, ax                  ; si = bias low
        mov     cx, dx                  ; cx = bias high (sign-extended)
; ---- slot 0 ----
        mov     ax, ss:_nn_acc+0
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
        mov     bx, 0
        call    FWD_SHIFT_ADD
        jmp     P1E_0
N0S_0:
        mov     bx, 0
        call    FWD_SHIFT_SUB
P1E_0:
        mov     ax, ss:_nn_acc+128
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
        mov     bx, 64
        call    FWD_SHIFT_ADD
        jmp     NXT_0
N1S_0:
        mov     bx, 64
        call    FWD_SHIFT_SUB
NXT_0:
; ---- slot 1 ----
        mov     ax, ss:_nn_acc+2
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
        mov     bx, 1
        call    FWD_SHIFT_ADD
        jmp     P1E_1
N0S_1:
        mov     bx, 1
        call    FWD_SHIFT_SUB
P1E_1:
        mov     ax, ss:_nn_acc+130
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
        mov     bx, 65
        call    FWD_SHIFT_ADD
        jmp     NXT_1
N1S_1:
        mov     bx, 65
        call    FWD_SHIFT_SUB
NXT_1:
; ---- slot 2 ----
        mov     ax, ss:_nn_acc+4
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
        mov     bx, 2
        call    FWD_SHIFT_ADD
        jmp     P1E_2
N0S_2:
        mov     bx, 2
        call    FWD_SHIFT_SUB
P1E_2:
        mov     ax, ss:_nn_acc+132
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
        mov     bx, 66
        call    FWD_SHIFT_ADD
        jmp     NXT_2
N1S_2:
        mov     bx, 66
        call    FWD_SHIFT_SUB
NXT_2:
; ---- slot 3 ----
        mov     ax, ss:_nn_acc+6
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
        mov     bx, 3
        call    FWD_SHIFT_ADD
        jmp     P1E_3
N0S_3:
        mov     bx, 3
        call    FWD_SHIFT_SUB
P1E_3:
        mov     ax, ss:_nn_acc+134
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
        mov     bx, 67
        call    FWD_SHIFT_ADD
        jmp     NXT_3
N1S_3:
        mov     bx, 67
        call    FWD_SHIFT_SUB
NXT_3:
; ---- slot 4 ----
        mov     ax, ss:_nn_acc+8
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
        mov     bx, 4
        call    FWD_SHIFT_ADD
        jmp     P1E_4
N0S_4:
        mov     bx, 4
        call    FWD_SHIFT_SUB
P1E_4:
        mov     ax, ss:_nn_acc+136
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
        mov     bx, 68
        call    FWD_SHIFT_ADD
        jmp     NXT_4
N1S_4:
        mov     bx, 68
        call    FWD_SHIFT_SUB
NXT_4:
; ---- slot 5 ----
        mov     ax, ss:_nn_acc+10
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
        mov     bx, 5
        call    FWD_SHIFT_ADD
        jmp     P1E_5
N0S_5:
        mov     bx, 5
        call    FWD_SHIFT_SUB
P1E_5:
        mov     ax, ss:_nn_acc+138
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
        mov     bx, 69
        call    FWD_SHIFT_ADD
        jmp     NXT_5
N1S_5:
        mov     bx, 69
        call    FWD_SHIFT_SUB
NXT_5:
; ---- slot 6 ----
        mov     ax, ss:_nn_acc+12
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
        mov     bx, 6
        call    FWD_SHIFT_ADD
        jmp     P1E_6
N0S_6:
        mov     bx, 6
        call    FWD_SHIFT_SUB
P1E_6:
        mov     ax, ss:_nn_acc+140
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
        mov     bx, 70
        call    FWD_SHIFT_ADD
        jmp     NXT_6
N1S_6:
        mov     bx, 70
        call    FWD_SHIFT_SUB
NXT_6:
; ---- slot 7 ----
        mov     ax, ss:_nn_acc+14
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
        mov     bx, 7
        call    FWD_SHIFT_ADD
        jmp     P1E_7
N0S_7:
        mov     bx, 7
        call    FWD_SHIFT_SUB
P1E_7:
        mov     ax, ss:_nn_acc+142
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
        mov     bx, 71
        call    FWD_SHIFT_ADD
        jmp     NXT_7
N1S_7:
        mov     bx, 71
        call    FWD_SHIFT_SUB
NXT_7:
; ---- slot 8 ----
        mov     ax, ss:_nn_acc+16
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
        mov     bx, 8
        call    FWD_SHIFT_ADD
        jmp     P1E_8
N0S_8:
        mov     bx, 8
        call    FWD_SHIFT_SUB
P1E_8:
        mov     ax, ss:_nn_acc+144
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
        mov     bx, 72
        call    FWD_SHIFT_ADD
        jmp     NXT_8
N1S_8:
        mov     bx, 72
        call    FWD_SHIFT_SUB
NXT_8:
; ---- slot 9 ----
        mov     ax, ss:_nn_acc+18
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
        mov     bx, 9
        call    FWD_SHIFT_ADD
        jmp     P1E_9
N0S_9:
        mov     bx, 9
        call    FWD_SHIFT_SUB
P1E_9:
        mov     ax, ss:_nn_acc+146
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
        mov     bx, 73
        call    FWD_SHIFT_ADD
        jmp     NXT_9
N1S_9:
        mov     bx, 73
        call    FWD_SHIFT_SUB
NXT_9:
; ---- slot 10 ----
        mov     ax, ss:_nn_acc+20
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
        mov     bx, 10
        call    FWD_SHIFT_ADD
        jmp     P1E_10
N0S_10:
        mov     bx, 10
        call    FWD_SHIFT_SUB
P1E_10:
        mov     ax, ss:_nn_acc+148
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
        mov     bx, 74
        call    FWD_SHIFT_ADD
        jmp     NXT_10
N1S_10:
        mov     bx, 74
        call    FWD_SHIFT_SUB
NXT_10:
; ---- slot 11 ----
        mov     ax, ss:_nn_acc+22
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
        mov     bx, 11
        call    FWD_SHIFT_ADD
        jmp     P1E_11
N0S_11:
        mov     bx, 11
        call    FWD_SHIFT_SUB
P1E_11:
        mov     ax, ss:_nn_acc+150
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
        mov     bx, 75
        call    FWD_SHIFT_ADD
        jmp     NXT_11
N1S_11:
        mov     bx, 75
        call    FWD_SHIFT_SUB
NXT_11:
; ---- slot 12 ----
        mov     ax, ss:_nn_acc+24
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
        mov     bx, 12
        call    FWD_SHIFT_ADD
        jmp     P1E_12
N0S_12:
        mov     bx, 12
        call    FWD_SHIFT_SUB
P1E_12:
        mov     ax, ss:_nn_acc+152
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
        mov     bx, 76
        call    FWD_SHIFT_ADD
        jmp     NXT_12
N1S_12:
        mov     bx, 76
        call    FWD_SHIFT_SUB
NXT_12:
; ---- slot 13 ----
        mov     ax, ss:_nn_acc+26
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
        mov     bx, 13
        call    FWD_SHIFT_ADD
        jmp     P1E_13
N0S_13:
        mov     bx, 13
        call    FWD_SHIFT_SUB
P1E_13:
        mov     ax, ss:_nn_acc+154
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
        mov     bx, 77
        call    FWD_SHIFT_ADD
        jmp     NXT_13
N1S_13:
        mov     bx, 77
        call    FWD_SHIFT_SUB
NXT_13:
; ---- slot 14 ----
        mov     ax, ss:_nn_acc+28
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
        mov     bx, 14
        call    FWD_SHIFT_ADD
        jmp     P1E_14
N0S_14:
        mov     bx, 14
        call    FWD_SHIFT_SUB
P1E_14:
        mov     ax, ss:_nn_acc+156
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
        mov     bx, 78
        call    FWD_SHIFT_ADD
        jmp     NXT_14
N1S_14:
        mov     bx, 78
        call    FWD_SHIFT_SUB
NXT_14:
; ---- slot 15 ----
        mov     ax, ss:_nn_acc+30
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
        mov     bx, 15
        call    FWD_SHIFT_ADD
        jmp     P1E_15
N0S_15:
        mov     bx, 15
        call    FWD_SHIFT_SUB
P1E_15:
        mov     ax, ss:_nn_acc+158
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
        mov     bx, 79
        call    FWD_SHIFT_ADD
        jmp     NXT_15
N1S_15:
        mov     bx, 79
        call    FWD_SHIFT_SUB
NXT_15:
; ---- slot 16 ----
        mov     ax, ss:_nn_acc+32
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
        mov     bx, 16
        call    FWD_SHIFT_ADD
        jmp     P1E_16
N0S_16:
        mov     bx, 16
        call    FWD_SHIFT_SUB
P1E_16:
        mov     ax, ss:_nn_acc+160
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
        mov     bx, 80
        call    FWD_SHIFT_ADD
        jmp     NXT_16
N1S_16:
        mov     bx, 80
        call    FWD_SHIFT_SUB
NXT_16:
; ---- slot 17 ----
        mov     ax, ss:_nn_acc+34
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
        mov     bx, 17
        call    FWD_SHIFT_ADD
        jmp     P1E_17
N0S_17:
        mov     bx, 17
        call    FWD_SHIFT_SUB
P1E_17:
        mov     ax, ss:_nn_acc+162
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
        mov     bx, 81
        call    FWD_SHIFT_ADD
        jmp     NXT_17
N1S_17:
        mov     bx, 81
        call    FWD_SHIFT_SUB
NXT_17:
; ---- slot 18 ----
        mov     ax, ss:_nn_acc+36
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
        mov     bx, 18
        call    FWD_SHIFT_ADD
        jmp     P1E_18
N0S_18:
        mov     bx, 18
        call    FWD_SHIFT_SUB
P1E_18:
        mov     ax, ss:_nn_acc+164
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
        mov     bx, 82
        call    FWD_SHIFT_ADD
        jmp     NXT_18
N1S_18:
        mov     bx, 82
        call    FWD_SHIFT_SUB
NXT_18:
; ---- slot 19 ----
        mov     ax, ss:_nn_acc+38
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
        mov     bx, 19
        call    FWD_SHIFT_ADD
        jmp     P1E_19
N0S_19:
        mov     bx, 19
        call    FWD_SHIFT_SUB
P1E_19:
        mov     ax, ss:_nn_acc+166
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
        mov     bx, 83
        call    FWD_SHIFT_ADD
        jmp     NXT_19
N1S_19:
        mov     bx, 83
        call    FWD_SHIFT_SUB
NXT_19:
; ---- slot 20 ----
        mov     ax, ss:_nn_acc+40
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
        mov     bx, 20
        call    FWD_SHIFT_ADD
        jmp     P1E_20
N0S_20:
        mov     bx, 20
        call    FWD_SHIFT_SUB
P1E_20:
        mov     ax, ss:_nn_acc+168
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
        mov     bx, 84
        call    FWD_SHIFT_ADD
        jmp     NXT_20
N1S_20:
        mov     bx, 84
        call    FWD_SHIFT_SUB
NXT_20:
; ---- slot 21 ----
        mov     ax, ss:_nn_acc+42
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
        mov     bx, 21
        call    FWD_SHIFT_ADD
        jmp     P1E_21
N0S_21:
        mov     bx, 21
        call    FWD_SHIFT_SUB
P1E_21:
        mov     ax, ss:_nn_acc+170
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
        mov     bx, 85
        call    FWD_SHIFT_ADD
        jmp     NXT_21
N1S_21:
        mov     bx, 85
        call    FWD_SHIFT_SUB
NXT_21:
; ---- slot 22 ----
        mov     ax, ss:_nn_acc+44
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
        mov     bx, 22
        call    FWD_SHIFT_ADD
        jmp     P1E_22
N0S_22:
        mov     bx, 22
        call    FWD_SHIFT_SUB
P1E_22:
        mov     ax, ss:_nn_acc+172
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
        mov     bx, 86
        call    FWD_SHIFT_ADD
        jmp     NXT_22
N1S_22:
        mov     bx, 86
        call    FWD_SHIFT_SUB
NXT_22:
; ---- slot 23 ----
        mov     ax, ss:_nn_acc+46
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
        mov     bx, 23
        call    FWD_SHIFT_ADD
        jmp     P1E_23
N0S_23:
        mov     bx, 23
        call    FWD_SHIFT_SUB
P1E_23:
        mov     ax, ss:_nn_acc+174
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
        mov     bx, 87
        call    FWD_SHIFT_ADD
        jmp     NXT_23
N1S_23:
        mov     bx, 87
        call    FWD_SHIFT_SUB
NXT_23:
; ---- slot 24 ----
        mov     ax, ss:_nn_acc+48
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
        mov     bx, 24
        call    FWD_SHIFT_ADD
        jmp     P1E_24
N0S_24:
        mov     bx, 24
        call    FWD_SHIFT_SUB
P1E_24:
        mov     ax, ss:_nn_acc+176
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
        mov     bx, 88
        call    FWD_SHIFT_ADD
        jmp     NXT_24
N1S_24:
        mov     bx, 88
        call    FWD_SHIFT_SUB
NXT_24:
; ---- slot 25 ----
        mov     ax, ss:_nn_acc+50
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
        mov     bx, 25
        call    FWD_SHIFT_ADD
        jmp     P1E_25
N0S_25:
        mov     bx, 25
        call    FWD_SHIFT_SUB
P1E_25:
        mov     ax, ss:_nn_acc+178
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
        mov     bx, 89
        call    FWD_SHIFT_ADD
        jmp     NXT_25
N1S_25:
        mov     bx, 89
        call    FWD_SHIFT_SUB
NXT_25:
; ---- slot 26 ----
        mov     ax, ss:_nn_acc+52
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
        mov     bx, 26
        call    FWD_SHIFT_ADD
        jmp     P1E_26
N0S_26:
        mov     bx, 26
        call    FWD_SHIFT_SUB
P1E_26:
        mov     ax, ss:_nn_acc+180
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
        mov     bx, 90
        call    FWD_SHIFT_ADD
        jmp     NXT_26
N1S_26:
        mov     bx, 90
        call    FWD_SHIFT_SUB
NXT_26:
; ---- slot 27 ----
        mov     ax, ss:_nn_acc+54
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
        mov     bx, 27
        call    FWD_SHIFT_ADD
        jmp     P1E_27
N0S_27:
        mov     bx, 27
        call    FWD_SHIFT_SUB
P1E_27:
        mov     ax, ss:_nn_acc+182
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
        mov     bx, 91
        call    FWD_SHIFT_ADD
        jmp     NXT_27
N1S_27:
        mov     bx, 91
        call    FWD_SHIFT_SUB
NXT_27:
; ---- slot 28 ----
        mov     ax, ss:_nn_acc+56
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
        mov     bx, 28
        call    FWD_SHIFT_ADD
        jmp     P1E_28
N0S_28:
        mov     bx, 28
        call    FWD_SHIFT_SUB
P1E_28:
        mov     ax, ss:_nn_acc+184
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
        mov     bx, 92
        call    FWD_SHIFT_ADD
        jmp     NXT_28
N1S_28:
        mov     bx, 92
        call    FWD_SHIFT_SUB
NXT_28:
; ---- slot 29 ----
        mov     ax, ss:_nn_acc+58
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
        mov     bx, 29
        call    FWD_SHIFT_ADD
        jmp     P1E_29
N0S_29:
        mov     bx, 29
        call    FWD_SHIFT_SUB
P1E_29:
        mov     ax, ss:_nn_acc+186
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
        mov     bx, 93
        call    FWD_SHIFT_ADD
        jmp     NXT_29
N1S_29:
        mov     bx, 93
        call    FWD_SHIFT_SUB
NXT_29:
; ---- slot 30 ----
        mov     ax, ss:_nn_acc+60
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
        mov     bx, 30
        call    FWD_SHIFT_ADD
        jmp     P1E_30
N0S_30:
        mov     bx, 30
        call    FWD_SHIFT_SUB
P1E_30:
        mov     ax, ss:_nn_acc+188
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
        mov     bx, 94
        call    FWD_SHIFT_ADD
        jmp     NXT_30
N1S_30:
        mov     bx, 94
        call    FWD_SHIFT_SUB
NXT_30:
; ---- slot 31 ----
        mov     ax, ss:_nn_acc+62
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
        mov     bx, 31
        call    FWD_SHIFT_ADD
        jmp     P1E_31
N0S_31:
        mov     bx, 31
        call    FWD_SHIFT_SUB
P1E_31:
        mov     ax, ss:_nn_acc+190
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
        mov     bx, 95
        call    FWD_SHIFT_ADD
        jmp     NXT_31
N1S_31:
        mov     bx, 95
        call    FWD_SHIFT_SUB
NXT_31:
; ---- slot 32 ----
        mov     ax, ss:_nn_acc+64
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
        mov     bx, 32
        call    FWD_SHIFT_ADD
        jmp     P1E_32
N0S_32:
        mov     bx, 32
        call    FWD_SHIFT_SUB
P1E_32:
        mov     ax, ss:_nn_acc+192
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
        mov     bx, 96
        call    FWD_SHIFT_ADD
        jmp     NXT_32
N1S_32:
        mov     bx, 96
        call    FWD_SHIFT_SUB
NXT_32:
; ---- slot 33 ----
        mov     ax, ss:_nn_acc+66
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
        mov     bx, 33
        call    FWD_SHIFT_ADD
        jmp     P1E_33
N0S_33:
        mov     bx, 33
        call    FWD_SHIFT_SUB
P1E_33:
        mov     ax, ss:_nn_acc+194
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
        mov     bx, 97
        call    FWD_SHIFT_ADD
        jmp     NXT_33
N1S_33:
        mov     bx, 97
        call    FWD_SHIFT_SUB
NXT_33:
; ---- slot 34 ----
        mov     ax, ss:_nn_acc+68
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
        mov     bx, 34
        call    FWD_SHIFT_ADD
        jmp     P1E_34
N0S_34:
        mov     bx, 34
        call    FWD_SHIFT_SUB
P1E_34:
        mov     ax, ss:_nn_acc+196
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
        mov     bx, 98
        call    FWD_SHIFT_ADD
        jmp     NXT_34
N1S_34:
        mov     bx, 98
        call    FWD_SHIFT_SUB
NXT_34:
; ---- slot 35 ----
        mov     ax, ss:_nn_acc+70
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
        mov     bx, 35
        call    FWD_SHIFT_ADD
        jmp     P1E_35
N0S_35:
        mov     bx, 35
        call    FWD_SHIFT_SUB
P1E_35:
        mov     ax, ss:_nn_acc+198
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
        mov     bx, 99
        call    FWD_SHIFT_ADD
        jmp     NXT_35
N1S_35:
        mov     bx, 99
        call    FWD_SHIFT_SUB
NXT_35:
; ---- slot 36 ----
        mov     ax, ss:_nn_acc+72
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
        mov     bx, 36
        call    FWD_SHIFT_ADD
        jmp     P1E_36
N0S_36:
        mov     bx, 36
        call    FWD_SHIFT_SUB
P1E_36:
        mov     ax, ss:_nn_acc+200
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
        mov     bx, 100
        call    FWD_SHIFT_ADD
        jmp     NXT_36
N1S_36:
        mov     bx, 100
        call    FWD_SHIFT_SUB
NXT_36:
; ---- slot 37 ----
        mov     ax, ss:_nn_acc+74
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
        mov     bx, 37
        call    FWD_SHIFT_ADD
        jmp     P1E_37
N0S_37:
        mov     bx, 37
        call    FWD_SHIFT_SUB
P1E_37:
        mov     ax, ss:_nn_acc+202
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
        mov     bx, 101
        call    FWD_SHIFT_ADD
        jmp     NXT_37
N1S_37:
        mov     bx, 101
        call    FWD_SHIFT_SUB
NXT_37:
; ---- slot 38 ----
        mov     ax, ss:_nn_acc+76
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
        mov     bx, 38
        call    FWD_SHIFT_ADD
        jmp     P1E_38
N0S_38:
        mov     bx, 38
        call    FWD_SHIFT_SUB
P1E_38:
        mov     ax, ss:_nn_acc+204
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
        mov     bx, 102
        call    FWD_SHIFT_ADD
        jmp     NXT_38
N1S_38:
        mov     bx, 102
        call    FWD_SHIFT_SUB
NXT_38:
; ---- slot 39 ----
        mov     ax, ss:_nn_acc+78
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
        mov     bx, 39
        call    FWD_SHIFT_ADD
        jmp     P1E_39
N0S_39:
        mov     bx, 39
        call    FWD_SHIFT_SUB
P1E_39:
        mov     ax, ss:_nn_acc+206
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
        mov     bx, 103
        call    FWD_SHIFT_ADD
        jmp     NXT_39
N1S_39:
        mov     bx, 103
        call    FWD_SHIFT_SUB
NXT_39:
; ---- slot 40 ----
        mov     ax, ss:_nn_acc+80
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
        mov     bx, 40
        call    FWD_SHIFT_ADD
        jmp     P1E_40
N0S_40:
        mov     bx, 40
        call    FWD_SHIFT_SUB
P1E_40:
        mov     ax, ss:_nn_acc+208
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
        mov     bx, 104
        call    FWD_SHIFT_ADD
        jmp     NXT_40
N1S_40:
        mov     bx, 104
        call    FWD_SHIFT_SUB
NXT_40:
; ---- slot 41 ----
        mov     ax, ss:_nn_acc+82
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
        mov     bx, 41
        call    FWD_SHIFT_ADD
        jmp     P1E_41
N0S_41:
        mov     bx, 41
        call    FWD_SHIFT_SUB
P1E_41:
        mov     ax, ss:_nn_acc+210
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
        mov     bx, 105
        call    FWD_SHIFT_ADD
        jmp     NXT_41
N1S_41:
        mov     bx, 105
        call    FWD_SHIFT_SUB
NXT_41:
; ---- slot 42 ----
        mov     ax, ss:_nn_acc+84
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
        mov     bx, 42
        call    FWD_SHIFT_ADD
        jmp     P1E_42
N0S_42:
        mov     bx, 42
        call    FWD_SHIFT_SUB
P1E_42:
        mov     ax, ss:_nn_acc+212
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
        mov     bx, 106
        call    FWD_SHIFT_ADD
        jmp     NXT_42
N1S_42:
        mov     bx, 106
        call    FWD_SHIFT_SUB
NXT_42:
; ---- slot 43 ----
        mov     ax, ss:_nn_acc+86
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
        mov     bx, 43
        call    FWD_SHIFT_ADD
        jmp     P1E_43
N0S_43:
        mov     bx, 43
        call    FWD_SHIFT_SUB
P1E_43:
        mov     ax, ss:_nn_acc+214
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
        mov     bx, 107
        call    FWD_SHIFT_ADD
        jmp     NXT_43
N1S_43:
        mov     bx, 107
        call    FWD_SHIFT_SUB
NXT_43:
; ---- slot 44 ----
        mov     ax, ss:_nn_acc+88
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
        mov     bx, 44
        call    FWD_SHIFT_ADD
        jmp     P1E_44
N0S_44:
        mov     bx, 44
        call    FWD_SHIFT_SUB
P1E_44:
        mov     ax, ss:_nn_acc+216
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
        mov     bx, 108
        call    FWD_SHIFT_ADD
        jmp     NXT_44
N1S_44:
        mov     bx, 108
        call    FWD_SHIFT_SUB
NXT_44:
; ---- slot 45 ----
        mov     ax, ss:_nn_acc+90
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
        mov     bx, 45
        call    FWD_SHIFT_ADD
        jmp     P1E_45
N0S_45:
        mov     bx, 45
        call    FWD_SHIFT_SUB
P1E_45:
        mov     ax, ss:_nn_acc+218
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
        mov     bx, 109
        call    FWD_SHIFT_ADD
        jmp     NXT_45
N1S_45:
        mov     bx, 109
        call    FWD_SHIFT_SUB
NXT_45:
; ---- slot 46 ----
        mov     ax, ss:_nn_acc+92
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
        mov     bx, 46
        call    FWD_SHIFT_ADD
        jmp     P1E_46
N0S_46:
        mov     bx, 46
        call    FWD_SHIFT_SUB
P1E_46:
        mov     ax, ss:_nn_acc+220
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
        mov     bx, 110
        call    FWD_SHIFT_ADD
        jmp     NXT_46
N1S_46:
        mov     bx, 110
        call    FWD_SHIFT_SUB
NXT_46:
; ---- slot 47 ----
        mov     ax, ss:_nn_acc+94
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
        mov     bx, 47
        call    FWD_SHIFT_ADD
        jmp     P1E_47
N0S_47:
        mov     bx, 47
        call    FWD_SHIFT_SUB
P1E_47:
        mov     ax, ss:_nn_acc+222
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
        mov     bx, 111
        call    FWD_SHIFT_ADD
        jmp     NXT_47
N1S_47:
        mov     bx, 111
        call    FWD_SHIFT_SUB
NXT_47:
; ---- slot 48 ----
        mov     ax, ss:_nn_acc+96
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
        mov     bx, 48
        call    FWD_SHIFT_ADD
        jmp     P1E_48
N0S_48:
        mov     bx, 48
        call    FWD_SHIFT_SUB
P1E_48:
        mov     ax, ss:_nn_acc+224
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
        mov     bx, 112
        call    FWD_SHIFT_ADD
        jmp     NXT_48
N1S_48:
        mov     bx, 112
        call    FWD_SHIFT_SUB
NXT_48:
; ---- slot 49 ----
        mov     ax, ss:_nn_acc+98
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
        mov     bx, 49
        call    FWD_SHIFT_ADD
        jmp     P1E_49
N0S_49:
        mov     bx, 49
        call    FWD_SHIFT_SUB
P1E_49:
        mov     ax, ss:_nn_acc+226
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
        mov     bx, 113
        call    FWD_SHIFT_ADD
        jmp     NXT_49
N1S_49:
        mov     bx, 113
        call    FWD_SHIFT_SUB
NXT_49:
; ---- slot 50 ----
        mov     ax, ss:_nn_acc+100
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
        mov     bx, 50
        call    FWD_SHIFT_ADD
        jmp     P1E_50
N0S_50:
        mov     bx, 50
        call    FWD_SHIFT_SUB
P1E_50:
        mov     ax, ss:_nn_acc+228
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
        mov     bx, 114
        call    FWD_SHIFT_ADD
        jmp     NXT_50
N1S_50:
        mov     bx, 114
        call    FWD_SHIFT_SUB
NXT_50:
; ---- slot 51 ----
        mov     ax, ss:_nn_acc+102
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
        mov     bx, 51
        call    FWD_SHIFT_ADD
        jmp     P1E_51
N0S_51:
        mov     bx, 51
        call    FWD_SHIFT_SUB
P1E_51:
        mov     ax, ss:_nn_acc+230
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
        mov     bx, 115
        call    FWD_SHIFT_ADD
        jmp     NXT_51
N1S_51:
        mov     bx, 115
        call    FWD_SHIFT_SUB
NXT_51:
; ---- slot 52 ----
        mov     ax, ss:_nn_acc+104
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
        mov     bx, 52
        call    FWD_SHIFT_ADD
        jmp     P1E_52
N0S_52:
        mov     bx, 52
        call    FWD_SHIFT_SUB
P1E_52:
        mov     ax, ss:_nn_acc+232
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
        mov     bx, 116
        call    FWD_SHIFT_ADD
        jmp     NXT_52
N1S_52:
        mov     bx, 116
        call    FWD_SHIFT_SUB
NXT_52:
; ---- slot 53 ----
        mov     ax, ss:_nn_acc+106
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
        mov     bx, 53
        call    FWD_SHIFT_ADD
        jmp     P1E_53
N0S_53:
        mov     bx, 53
        call    FWD_SHIFT_SUB
P1E_53:
        mov     ax, ss:_nn_acc+234
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
        mov     bx, 117
        call    FWD_SHIFT_ADD
        jmp     NXT_53
N1S_53:
        mov     bx, 117
        call    FWD_SHIFT_SUB
NXT_53:
; ---- slot 54 ----
        mov     ax, ss:_nn_acc+108
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
        mov     bx, 54
        call    FWD_SHIFT_ADD
        jmp     P1E_54
N0S_54:
        mov     bx, 54
        call    FWD_SHIFT_SUB
P1E_54:
        mov     ax, ss:_nn_acc+236
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
        mov     bx, 118
        call    FWD_SHIFT_ADD
        jmp     NXT_54
N1S_54:
        mov     bx, 118
        call    FWD_SHIFT_SUB
NXT_54:
; ---- slot 55 ----
        mov     ax, ss:_nn_acc+110
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
        mov     bx, 55
        call    FWD_SHIFT_ADD
        jmp     P1E_55
N0S_55:
        mov     bx, 55
        call    FWD_SHIFT_SUB
P1E_55:
        mov     ax, ss:_nn_acc+238
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
        mov     bx, 119
        call    FWD_SHIFT_ADD
        jmp     NXT_55
N1S_55:
        mov     bx, 119
        call    FWD_SHIFT_SUB
NXT_55:
; ---- slot 56 ----
        mov     ax, ss:_nn_acc+112
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
        mov     bx, 56
        call    FWD_SHIFT_ADD
        jmp     P1E_56
N0S_56:
        mov     bx, 56
        call    FWD_SHIFT_SUB
P1E_56:
        mov     ax, ss:_nn_acc+240
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
        mov     bx, 120
        call    FWD_SHIFT_ADD
        jmp     NXT_56
N1S_56:
        mov     bx, 120
        call    FWD_SHIFT_SUB
NXT_56:
; ---- slot 57 ----
        mov     ax, ss:_nn_acc+114
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
        mov     bx, 57
        call    FWD_SHIFT_ADD
        jmp     P1E_57
N0S_57:
        mov     bx, 57
        call    FWD_SHIFT_SUB
P1E_57:
        mov     ax, ss:_nn_acc+242
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
        mov     bx, 121
        call    FWD_SHIFT_ADD
        jmp     NXT_57
N1S_57:
        mov     bx, 121
        call    FWD_SHIFT_SUB
NXT_57:
; ---- slot 58 ----
        mov     ax, ss:_nn_acc+116
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
        mov     bx, 58
        call    FWD_SHIFT_ADD
        jmp     P1E_58
N0S_58:
        mov     bx, 58
        call    FWD_SHIFT_SUB
P1E_58:
        mov     ax, ss:_nn_acc+244
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
        mov     bx, 122
        call    FWD_SHIFT_ADD
        jmp     NXT_58
N1S_58:
        mov     bx, 122
        call    FWD_SHIFT_SUB
NXT_58:
; ---- slot 59 ----
        mov     ax, ss:_nn_acc+118
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
        mov     bx, 59
        call    FWD_SHIFT_ADD
        jmp     P1E_59
N0S_59:
        mov     bx, 59
        call    FWD_SHIFT_SUB
P1E_59:
        mov     ax, ss:_nn_acc+246
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
        mov     bx, 123
        call    FWD_SHIFT_ADD
        jmp     NXT_59
N1S_59:
        mov     bx, 123
        call    FWD_SHIFT_SUB
NXT_59:
; ---- slot 60 ----
        mov     ax, ss:_nn_acc+120
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
        mov     bx, 60
        call    FWD_SHIFT_ADD
        jmp     P1E_60
N0S_60:
        mov     bx, 60
        call    FWD_SHIFT_SUB
P1E_60:
        mov     ax, ss:_nn_acc+248
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
        mov     bx, 124
        call    FWD_SHIFT_ADD
        jmp     NXT_60
N1S_60:
        mov     bx, 124
        call    FWD_SHIFT_SUB
NXT_60:
; ---- slot 61 ----
        mov     ax, ss:_nn_acc+122
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
        mov     bx, 61
        call    FWD_SHIFT_ADD
        jmp     P1E_61
N0S_61:
        mov     bx, 61
        call    FWD_SHIFT_SUB
P1E_61:
        mov     ax, ss:_nn_acc+250
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
        mov     bx, 125
        call    FWD_SHIFT_ADD
        jmp     NXT_61
N1S_61:
        mov     bx, 125
        call    FWD_SHIFT_SUB
NXT_61:
; ---- slot 62 ----
        mov     ax, ss:_nn_acc+124
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
        mov     bx, 62
        call    FWD_SHIFT_ADD
        jmp     P1E_62
N0S_62:
        mov     bx, 62
        call    FWD_SHIFT_SUB
P1E_62:
        mov     ax, ss:_nn_acc+252
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
        mov     bx, 126
        call    FWD_SHIFT_ADD
        jmp     NXT_62
N1S_62:
        mov     bx, 126
        call    FWD_SHIFT_SUB
NXT_62:
; ---- slot 63 ----
        mov     ax, ss:_nn_acc+126
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
        mov     bx, 63
        call    FWD_SHIFT_ADD
        jmp     P1E_63
N0S_63:
        mov     bx, 63
        call    FWD_SHIFT_SUB
P1E_63:
        mov     ax, ss:_nn_acc+254
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
        mov     bx, 127
        call    FWD_SHIFT_ADD
        jmp     NXT_63
N1S_63:
        mov     bx, 127
        call    FWD_SHIFT_SUB
NXT_63:
; --- finish: (si:cx >> NNUE_SCALE_SHIFT) low word, negate for black ---
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
        test    bp, bp
        jz      FWD_RET
        neg     ax
FWD_RET:
        pop     ds
        pop     di
        pop     si
        pop     bx
        pop     bp
        retf
nn_fwd_eval_ ENDP

; shared shift handlers: si:cx += w2[bx]<<7   (bx = slot, or 64+slot for POV 1)
FWD_SHIFT_ADD:
        mov     al, ss:_nn_w2[bx]
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
        mov     al, ss:_nn_w2[bx]
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
