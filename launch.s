# Replacement game-launch routine for GameShark Pro v3.30 (shell.bin)
#
# Placed at 0x80205C00, over the PC-link "receive and run" helpers
# 0x80205C00-0x80205D47 which nothing in the firmware calls (82 words free,
# same address in both supported builds). The original routine is replaced
# by "j 0x80205C00 ; nop". This routine never returns, so it saves no
# registers. It is written without relying on branch delay slots; the
# assembler fills them with nops.
#
# The symbols below differ between the two v3.30 builds; the routine is
# assembled once per build (base address 0x80205C00) with these values:
#
#   symbol     meaning                          09:54 Mar 27   15:56 Apr 4
#   DE_HI      %hi of the DE-code variables     0x8022         0x8023
#   DE_FLAG    %lo of "DE code present"         0x7BD0         -0x7DA0
#   DE_ENTRY   %lo of "DE entry point"          0x7BD4         -0x7D9C
#   STUB_HI    %hi of the loader stub           0x8022         0x8022
#   STUB_LO    %lo of the loader stub           -0x7BF4        -0x7574
#              (stub address)                   (0x8021840C)   (0x80218A8C)
#   LIST_HI    %hi of the F0/F1 patch list      0x8025         0x8025
#   LIST_LO    %lo of the F0/F1 patch list      -0x4EF0        -0x4840
#              (list address)                   (0x8024B110)   (0x8024B7C0)
#   FLUSH      cache write-back routine         0x8021853C     0x80218BBC
#
# Differences from the original routine:
#   * detects the CIC-NUS-5101 (Aleck64) IPL3 by two signature words
#   * for such games subtracts 0x100000 from the header entry point, like the
#     real 5101 IPL3 does (unless a DE code overrides the entry point)
#   * when the resulting entry is 0x80000400 it copies 0x3FE000 bytes instead
#     of 1 MB, and places the loader stub and the F0/F1 patch list just below
#     the current stack (top of 4 MB RAM, above the copied area)
#   * passes the word count to the stub in $a3 (the stub is patched to use it)

launch:
    lui     $t0, 0xb000
    lw      $s0, 8($t0)              # raw entry point from the game header
    lui     $v0, DE_HI
    lw      $v1, DE_FLAG($v0)         # DE code present?
    beqz    $v1, no_override
    lw      $s0, DE_ENTRY($v0)         # entry point from the DE code
no_override:
    lui     $s4, 4                   # word count = 0x40000 (1 MB)

    # CIC-NUS-5101 IPL3 signature: words at ROM 0x524/0x528
    lw      $t1, 0x524($t0)
    lui     $t2, 0x3c0a
    ori     $t2, $t2, 0x003f
    bne     $t1, $t2, generic
    lw      $t1, 0x528($t0)
    lui     $t2, 0x354a
    ori     $t2, $t2, 0xdfff
    bne     $t1, $t2, generic
    lui     $t2, 0x10
    bnez    $v1, no_adjust           # keep a DE override as it is
    subu    $s0, $s0, $t2            # entry -= 0x100000 (5101 behaviour)
no_adjust:
    lui     $t2, 0x8000
    ori     $t2, $t2, 0x0400
    bne     $s0, $t2, generic        # 0x3FE000-byte copy only for 0x80000400
    lui     $s4, 0x000f
    ori     $s4, $s4, 0xf800         # word count = 0xFF800 (0x3FE000 bytes)
    addiu   $s2, $sp, -0x300         # stub: 0x100 bytes below the stack
    addiu   $s3, $s2, 0x100          # patch list: 0x190 bytes after it
    b       copy

generic:
    lui     $v0, 0x8010
    sltu    $v0, $v0, $s0            # entry > 0x80100000 ?
    lui     $t2, 0x10
    beqz    $v0, low_entry
    lui     $s2, 0x8000
    ori     $s2, $s2, 0x0400         # stub at 0x80000400
    b       generic2
low_entry:
    addu    $s2, $s0, $t2            # stub at entry + 0x100000
generic2:
    addiu   $s3, $s2, 0x1000         # patch list at stub + 0x1000

copy:
    # copy the loader stub (0x40 words) from 0x8021840c
    move    $v1, $zero
    move    $a1, $s2
    lui     $a0, STUB_HI
    addiu   $a0, $a0, STUB_LO
copy_stub:
    lw      $v0, 0($a0)
    addiu   $a0, $a0, 4
    addiu   $v1, $v1, 1
    sw      $v0, 0($a1)
    addiu   $a1, $a1, 4
    slti    $v0, $v1, 0x40
    bnez    $v0, copy_stub

    # copy the boot-time patch list (0x64 words) from 0x8024b110
    move    $v1, $zero
    move    $a1, $s3
    lui     $a0, LIST_HI
    addiu   $a0, $a0, LIST_LO
copy_list:
    lw      $v0, 0($a0)
    addiu   $a0, $a0, 4
    addiu   $v1, $v1, 1
    sw      $v0, 0($a1)
    addiu   $a1, $a1, 4
    slti    $v0, $v1, 0x64
    bnez    $v0, copy_list

    jal     FLUSH                    # write back / invalidate caches

    lui     $s1, 0xb000
    ori     $s1, $s1, 0x1000         # source: cart ROM + 0x1000
    move    $a0, $s0                 # destination / entry point
    move    $a1, $s1
    move    $a2, $s3                 # patch list
    move    $a3, $s4                 # word count
    jalr    $s2                      # run the stub (does not return)
hang:
    b       hang
