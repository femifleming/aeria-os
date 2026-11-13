/* AERIA OS entry. The bootstrap (or Modwerk's RAM-boot gate) jumps here at 0x40000400 with jsr.
 * The first instruction must be `lea -28(%sp),%sp` (0x4FEFFFE4): the RAM-boot gate checks for it. */
    .section .text.entry, "ax"
    .globl _start
_start:
    lea     -28(%sp), %sp           /* the word the gate looks for; harmless */
    move.w  #0x2700, %sr            /* every interrupt masked: AERIA OS polls */
    move.l  #0x47E00000, %sp        /* our stack, below the log at 0x47F00000 */
    /* the caches, as stock runs them (HARDWARE.md 1.4), BEFORE anything is written: invalidated and on, SDRAM
     * copyback through ACR0. They used to be set up in aeria_main, after .bss had been cleared -- and when the
     * cache was already on (a start from RAM over the link) that invalidation threw away the last 16 KB of the
     * clearing while it still sat in the cache: the variables at the end of .bss kept whatever the RAM held
     * (found 12 Nov 2026: `hidden` came up -1 and the screen stayed dark). */
    move.l  #0x4007E020, %d0
    movec   %d0, %acr0
    moveq   #0, %d0
    movec   %d0, %acr1
    move.l  #0xA50CE100, %d0
    movec   %d0, %cacr
    nop
    /* zero .bss */
    lea     __bss_start, %a0
    lea     __bss_end, %a1
1:  cmpa.l  %a1, %a0
    bcc.s   2f
    clr.l   (%a0)+
    bra.s   1b
    /* the vector table: the 1 KB below the image (0x40000000), as stock keeps it; every entry -> fault */
2:  move.l  #0x40000000, %a0
    move.l  #fault_entry, %d0
    move.l  #256, %d1
3:  move.l  %d0, (%a0)+
    subq.l  #1, %d1
    bne.s   3b
    move.l  #0x40000000, %d0
    movec   %d0, %vbr
    jsr     aeria_main
4:  bra.s   4b

/* Any exception: the CPU stacked the format/vector word and the PC. Record them and hand back. */
fault_entry:
    move.w  #0x2700, %sr
    move.l  (%sp), %d0              /* format word: vector offset in bits 25..18 */
    move.l  4(%sp), %d1             /* PC */
    move.l  #0x47E00000, %sp
    move.l  %d1, -(%sp)
    move.l  %d0, -(%sp)
    jsr     aeria_fault
5:  bra.s   5b

#ifdef HYBRID
/* hybrid bring-up: stock's PIT0 time-slice ISR (0x40000550) is patched to jump here. After tick_limit ticks of a
 * normally booting stock OS, AERIA OS takes over (interrupts off) and runs its audio test. */
    .data
    .globl tick_count, tick_limit
tick_count: .long 0
tick_limit: .long 0x7fffffff
    .globl ring_start, hold_ticks
hold_ticks: .long 0
ring_start: .long 0x7fffffff
    .text
    .globl tick_hook
tick_hook:
    move.w  #0x2700, %sr
    move.l  %a0, 0x80006900
    move.l  %d0, -(%sp)
    move.l  %d1, -(%sp)
    move.l  tick_count, %d0
    addq.l  #1, %d0
    move.l  %d0, tick_count
    /* the interrupted PC of each of the last 110 ticks, to battery RAM 0x100FB620.. (what stock was running) */
    move.l  %d0, %d1
    sub.l   ring_start, %d1
    bmi.s   2f
    lsl.l   #2, %d1
    move.l  #0x100FB620, %a0
    add.l   %d1, %a0
    move.l  12(%sp), %d1
    move.l  %d1, (%a0)
2:  cmp.l   tick_limit, %d0
    bcc.s   1f
    move.l  (%sp)+, %d1
    move.l  (%sp)+, %d0
    jmp     0x4000055a
1:  move.l  %d0, 0x100FB7E0
    /* bring-up: hold here (interrupts off, stock frozen) for hold_ticks bus cycles before AERIA OS starts */
    move.l  hold_ticks, %d1
    beq.s   4f
    move.l  0xFC07C00C, %d0
3:  move.l  0xFC07C00C, %a0
    sub.l   %d0, %a0
    cmp.l   %d1, %a0
    bcs.s   3b
4:
    move.l  #0x5741524D, %d0            /* 'WARM': skip the panel start-up, stock left the panel running */
    move.l  %d0, 0x100FB7F0
    move.l  0xFC07C00C, %d0
    move.l  %d0, 0x100FB7E4
    jmp     _start
#endif
