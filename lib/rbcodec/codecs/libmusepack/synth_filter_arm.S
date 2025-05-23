/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2008 by Andree Buschmann
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include "config.h"

#if defined(CPU_S5L87XX)
    /* Moving this asm routine to iram is (little) faster on S5L87xx. */
    .section .icode, "ax", %progbits
#else
    .section .text, "ax", %progbits
#endif

/****************************************************************************
 * void mpc_decoder_windowing_D(...)
 * 
 * 2nd step within synthesis filter. Does the dewindowing.
 * 64=32x32 multiplies
 * Uses un-shifted D[]-values. D[] will always be the second operand of 
 * smull/smlal to achieve higher speed as D[] has lower amplitude than V[].
 ****************************************************************************/
    .align  2
    .global mpc_decoder_windowing_D
    .type   mpc_decoder_windowing_D, %function
#if 0
mpc_decoder_windowing_D:
    /* r0 = Data[] */
    /* r1 = V[] */
    /* r2 = D[] */
    /* lr = counter */
    /************************************************************************
     * Reference implementation.
     ***********************************************************************/
    stmfd   sp!, {r4-r8, lr}
    
    mov lr, #32
.loop32:
    ldmia r2!, { r3-r6 }    /* load D[00..03] */
    ldr r7, [r1]            /*  0 */
    smull r8, r12, r7, r3
    ldr r7, [r1, #96*4]     /*  1 */
    smlal r8, r12, r7, r4
    ldr r7, [r1, #128*4]    /*  2 */
    smlal r8, r12, r7, r5
    ldr r7, [r1, #224*4]    /*  3 */
    smlal r8, r12, r7, r6
    ldmia r2!, { r3-r6 }    /* load D[04..07] */
    ldr r7, [r1, #256*4]    /*  4 */
    smlal r8, r12, r7, r3
    ldr r7, [r1, #352*4]    /*  5 */
    smlal r8, r12, r7, r4
    ldr r7, [r1, #384*4]    /*  6 */
    smlal r8, r12, r7, r5
    ldr r7, [r1, #480*4]    /*  7 */
    smlal r8, r12, r7, r6
    ldmia r2!, { r3-r6 }    /* load D[08..11] */
    ldr r7, [r1, #512*4]    /*  8 */
    smlal r8, r12, r7, r3
    ldr r7, [r1, #608*4]    /*  9 */
    smlal r8, r12, r7, r4
    ldr r7, [r1, #640*4]    /* 10 */
    smlal r8, r12, r7, r5
    ldr r7, [r1, #736*4]    /* 11 */
    smlal r8, r12, r7, r6
    ldmia r2!, { r3-r6 }    /* load D[12..15] */
    ldr r7, [r1, #768*4]    /* 12 */
    smlal r8, r12, r7, r3
    ldr r7, [r1, #864*4]    /* 13 */
    smlal r8, r12, r7, r4
    ldr r7, [r1, #896*4]    /* 14 */
    smlal r8, r12, r7, r5
    ldr r7, [r1, #992*4]    /* 15 */
    smlal r8, r12, r7, r6
    mov r8, r8, lsr #16
    orr r8, r8, r12, lsl #16 /* (lo>>16) || (hi<<16) */
    str r8, [r0], #4        /* store Data */
    add r1, r1, #4          /* V++ */
    
    subs lr, lr, #1
    bgt .loop32
    
    ldmpc regs=r4-r8
#elif defined(CPU_ARM7TDMI) /* arm7 only */
mpc_decoder_windowing_D:
    /* r0 = Data[] */
    /* r1 = V[] */
    /* r2 = D[] */
    /* lr = counter */
    /************************************************************************
     * Further speed up through making use of symmetries within D[]-window.
     * The row V[00] can be extracted as it has symmetries within this single
     * row. 8 smull/mlal and 8 ldr's can be saved at the cost of 2 add's.
     * The rows V[01..15] are symmetric to V[31..17]. 15 x 16 ldr's can be
     * saved at the cost of 15 x 4 + 1 add's.
     * The row V[16] can be extracted as it has symmetries within this single
     * row. 8 smull/mlal and 8 ldr's can be saved.
     * Used for arm7 only. For arm9 and above see implementation below.
     ***********************************************************************/
    stmfd   sp!, {r4-r11, lr}
    
    /******************************************
     * row 0 with internal symmetry
     *****************************************/
    add r2, r2, #4          /* D+=1, r2 = D[01] as D[00] = 0 */
    ldmia r2!, { r3-r6 }    /* load D[01..04] */
    ldr r7 , [r1, #96*4]    /*  1 */
    ldr r10, [r1, #992*4]   /* 15 */
    rsb r10, r10, r7        /* V[01] - V[15] */
    smull r8, r9, r10, r3
    ldr r7 , [r1, #128*4]   /*  2 */
    ldr r10, [r1, #896*4]   /* 14 */
    add r10, r10, r7        /* V[02] + V[14] */
    smlal r8, r9, r10, r4
    ldr r7 , [r1, #224*4]   /*  3 */
    ldr r10, [r1, #864*4]   /* 13 */
    rsb r10, r10, r7        /* V[03] - V[13] */
    smlal r8, r9, r10, r5
    ldr r7 , [r1, #256*4]   /*  4 */
    ldr r10, [r1, #768*4]   /* 12 */
    add r10, r10, r7        /* V[04] + V[12] */
    smlal r8, r9, r10, r6
    ldmia r2!, { r3-r6 }    /* load D[05..08] */
    ldr r7 , [r1, #352*4]   /*  5 */
    ldr r10, [r1, #736*4]   /* 11 */
    rsb r10, r10, r7        /* V[05] - V[11] */
    smlal r8, r9, r10, r3
    ldr r7 , [r1, #384*4]   /*  6 */
    ldr r10, [r1, #640*4]   /* 10 */
    add r10, r10, r7        /* V[06] + V[10] */
    smlal r8, r9, r10, r4
    ldr r7 , [r1, #480*4]   /*  7 */
    ldr r10, [r1, #608*4]   /*  9 */
    rsb r10, r10, r7        /* V[07] - V[09] */
    smlal r8, r9, r10, r5
    ldr r10, [r1, #512*4]   /*  8 */
    smlal r8, r9, r10, r6
    mov r8, r8, lsr #16
    orr r8, r8, r9, lsl #16 /* (lo>>16) || (hi<<16) */
    str r8, [r0], #4        /* store Data */
    add r1, r1, #4          /* V+=1, r1 = V[01] */
    add r2, r2, #7*4        /* D+=7, r2 = D[16] */
    
    /******************************************
     * rows 01..15 are symmetric to rows 31..17
     * r8  = lo, r9  = hi of 01..15
     * r1  = V[01..15]
     * r10 = lo, r11 = hi of 31..17
     * r12 = V[31..16]
     *****************************************/
    mov lr, #15*8
    add r12, r1, #30*4          /* r12 = V[31] */
.loop15:
    ldmia r2!, { r3-r6 }        /* load D[00..03] */
    ldr r7, [r12, #768*4]       /* 12 */
    smull r10, r11, r7, r6
    ldr r7, [r12, #864*4]       /* 13 */
    smlal r10, r11, r7, r5
    ldr r7, [r12, #896*4]       /* 14 */
    smlal r10, r11, r7, r4
    ldr r7, [r12, #992*4]       /* 15 */
    smlal r10, r11, r7, r3
    ldr r7, [r1]                /*  0 */
    smull r8, r9, r7, r3
    ldr r7, [r1, #96*4]         /*  1 */
    smlal r8, r9, r7, r4
    ldr r7, [r1, #128*4]        /*  2 */
    smlal r8, r9, r7, r5
    ldr r7, [r1, #224*4]        /*  3 */
    smlal r8, r9, r7, r6
    ldmia r2!, { r3-r6 }        /* load D[04..07] */
    ldr r7, [r1, #256*4]        /*  4 */
    smlal r8, r9, r7, r3
    ldr r7, [r1, #352*4]        /*  5 */
    smlal r8, r9, r7, r4
    ldr r7, [r1, #384*4]        /*  6 */
    smlal r8, r9, r7, r5
    ldr r7, [r1, #480*4]        /*  7 */
    smlal r8, r9, r7, r6
    ldr r7, [r12, #512*4]       /*  8 */
    smlal r10, r11, r7, r6
    ldr r7, [r12, #608*4]       /*  9 */
    smlal r10, r11, r7, r5
    ldr r7, [r12, #640*4]       /* 10 */
    smlal r10, r11, r7, r4
    ldr r7, [r12, #736*4]       /* 11 */
    smlal r10, r11, r7, r3
    ldmia r2!, { r3-r6 }        /* load D[08..11] */
    ldr r7, [r12, #256*4]       /*  4 */
    smlal r10, r11, r7, r6
    ldr r7, [r12, #352*4]       /*  5 */
    smlal r10, r11, r7, r5
    ldr r7, [r12, #384*4]       /*  6 */
    smlal r10, r11, r7, r4
    ldr r7, [r12, #480*4]       /*  7 */
    smlal r10, r11, r7, r3
    ldr r7, [r1, #512*4]        /*  8 */
    smlal r8, r9, r7, r3
    ldr r7, [r1, #608*4]        /*  9 */
    smlal r8, r9, r7, r4
    ldr r7, [r1, #640*4]        /* 10 */
    smlal r8, r9, r7, r5
    ldr r7, [r1, #736*4]        /* 11 */
    smlal r8, r9, r7, r6
    ldmia r2!, { r3-r6 }        /* load D[12..15] */
    ldr r7, [r1, #768*4]        /* 12 */
    smlal r8, r9, r7, r3
    ldr r7, [r1, #864*4]        /* 13 */
    smlal r8, r9, r7, r4
    ldr r7, [r1, #896*4]        /* 14 */
    smlal r8, r9, r7, r5
    ldr r7, [r1, #992*4]        /* 15 */
    smlal r8, r9, r7, r6
    ldr r7, [r12]               /*  0 */
    smlal r10, r11, r7, r6
    ldr r7, [r12, #96*4]        /*  1 */
    smlal r10, r11, r7, r5
    ldr r7, [r12, #128*4]       /*  2 */
    smlal r10, r11, r7, r4
    ldr r7, [r12, #224*4]       /*  3 */
    smlal r10, r11, r7, r3
    /* store Data[01..15] */
    mov r8, r8, lsr #16
    orr r8, r8, r9, lsl #16     /* (lo>>16) || (hi<<16) */
    /* store Data[31..17] */
    mov r10, r10, lsr #16
    orr r10, r10, r11, lsl #16  /* (lo>>16) || (hi<<16) */
    rsb r10, r10, #0            /* r10 = -r10 */
    str r10, [r0, lr]           /* store Data */
    str r8, [r0], #4            /* store Data */
    /* correct adresses for next loop */
    sub r12, r12, #4            /* r12 = V-- */
    add r1, r1, #4              /* r1  = V++ */
    /* next loop */
    subs lr, lr, #8
    bgt .loop15

    /******************************************
     * V[16] with internal symmetry
     *****************************************/
    ldmia r2!, { r3-r6 }        /* load D[00..03] */
    ldr r7 , [r1]               /*  0 */
    ldr r10, [r1, #992*4]       /* 15 */
    rsb r10, r10, r7            /* V[00] - V[15] */
    smull r8, r9, r10, r3
    ldr r7 , [r1, #96*4]        /*  1 */
    ldr r10, [r1, #896*4]       /* 14 */
    rsb r10, r10, r7            /* V[01] - V[14] */
    smlal r8, r9, r10, r4
    ldr r7 , [r1, #128*4]       /*  2 */
    ldr r10, [r1, #864*4]       /* 13 */
    rsb r10, r10, r7            /* V[02] - V[13] */
    smlal r8, r9, r10, r5
    ldr r7 , [r1, #224*4]       /*  3 */
    ldr r10, [r1, #768*4]       /* 12 */
    rsb r10, r10, r7            /* V[03] - V[12] */
    smlal r8, r9, r10, r6
    ldmia r2!, { r3-r6 }        /* load D[04..07] */
    ldr r7 , [r1, #256*4]       /*  4 */
    ldr r10, [r1, #736*4]       /* 11 */
    rsb r10, r10, r7            /* V[04] - V[11] */
    smlal r8, r9, r10, r3
    ldr r7 , [r1, #352*4]       /*  5 */
    ldr r10, [r1, #640*4]       /* 10 */
    rsb r10, r10, r7            /* V[05] - V[10] */
    smlal r8, r9, r10, r4
    ldr r7 , [r1, #384*4]       /*  6 */
    ldr r10, [r1, #608*4]       /*  9 */
    rsb r10, r10, r7            /* V[06] - V[09] */
    smlal r8, r9, r10, r5
    ldr r7 , [r1, #480*4]       /*  7 */
    ldr r10, [r1, #512*4]       /*  8 */
    rsb r10, r10, r7            /* V[07] - V[08] */
    smlal r8, r9, r10, r6
    mov r8, r8, lsr #16
    orr r8, r8, r9, lsl #16     /* (lo>>16) || (hi<<16) */
    str r8, [r0], #4            /* store Data */
    
    ldmpc regs=r4-r11
#elif ARM_ARCH < 6 /* arm9 and above */
    mpc_decoder_windowing_D:
    /* r0 = Data[] */
    /* r1 = V[] */
    /* r2 = D[] */
    /* lr = counter */
    /************************************************************************
     * Further speed up through making use of symmetries within D[]-window.
     * The row V[00] can be extracted as it has symmetries within this single
     * row. 8 smull/mlal and 8 ldr's can be saved at the cost of 2 add's.
     * The rows V[01..15] are symmetric to V[31..17]. 15 x 16 ldr's can be
     * saved at the cost of 15 x 4 + 1 add's.
     * The row V[16] can be extracted as it has symmetries within this single
     * row. 8 smull/mlal and 8 ldr's can be saved.
     * On arm9 (still armv4 architecture) reducing stalls after ldr/ldm speeds
     * up decoding even though several ldm-calls are replaced with ldr to free
     * 2 registers.
     ***********************************************************************/
    stmfd   sp!, {r4-r11, lr}
    
    /******************************************
     * row 0 with internal symmetry
     *****************************************/
    add r2, r2, #4          /* D+=1, r2 = D[01] as D[00] = 0 */
    ldmia r2!, { r3-r6 }    /* load D[01..04] */
    ldr r7 , [r1, #96*4]    /*  1 */
    ldr r10, [r1, #992*4]   /* 15 */
    ldr r11, [r1, #128*4]   /*  2 */
    ldr r12, [r1, #896*4]   /* 14 */
    rsb r10, r10, r7        /* V[01] - V[15] */
    smull r8, r9, r10, r3
    ldr r7 , [r1, #224*4]   /*  3 */
    ldr r10, [r1, #864*4]   /* 13 */
    add r12, r12, r11       /* V[02] + V[14] */
    smlal r8, r9, r12, r4
    ldr r11, [r1, #256*4]   /*  4 */
    ldr r12, [r1, #768*4]   /* 12 */
    rsb r10, r10, r7        /* V[03] - V[13] */
    smlal r8, r9, r10, r5
    ldr r7 , [r1, #352*4]   /*  5 */
    ldr r10, [r1, #736*4]   /* 11 */
    add r12, r12, r11       /* V[04] + V[12] */
    smlal r8, r9, r12, r6
    ldmia r2!, { r3-r6 }    /* load D[05..08] */
    ldr r11, [r1, #384*4]   /*  6 */
    ldr r12, [r1, #640*4]   /* 10 */
    rsb r10, r10, r7        /* V[05] - V[11] */
    smlal r8, r9, r10, r3
    ldr r7 , [r1, #480*4]   /*  7 */
    ldr r10, [r1, #608*4]   /*  9 */
    add r12, r12, r11       /* V[06] + V[10] */
    smlal r8, r9, r12, r4
    ldr r11, [r1, #512*4]   /*  8 */
    rsb r10, r10, r7        /* V[07] - V[09] */
    smlal r8, r9, r10, r5
    smlal r8, r9, r11, r6
    mov r8, r8, lsr #16
    orr r8, r8, r9, lsl #16 /* (lo>>16) || (hi<<16) */
    str r8, [r0], #4        /* store Data */
    add r1, r1, #4          /* V+=1, r1 = V[01] */
    add r2, r2, #7*4        /* D+=7, r2 = D[16] */
    
    /******************************************
     * rows 01..15 are symmetric to rows 31..17
     * r8  = lo, r9  = hi of 01..15
     * r1  = V[01..15]
     * r10 = lo, r11 = hi of 31..17
     * r12 = V[31..16]
     *****************************************/
    mov lr, #15*8
    add r12, r1, #30*4          /* r12 = V[31] */
.loop15:
    ldmia r2!, { r3-r4 }        /* load D[00..01] */
    ldr r7, [r12, #896*4]       /* 14 */
    ldr r5, [r12, #992*4]       /* 15 */
    smull r10, r11, r7, r4
    ldr r7, [r1]                /*  0 */
    smlal r10, r11, r5, r3
    ldr r5, [r1, #96*4]         /*  1 */
    smull r8, r9, r7, r3
    ldr r7, [r12, #768*4]       /* 12 */
    smlal r8, r9, r5, r4
    ldmia r2!, { r3-r4 }        /* load D[02..03] */
    ldr r5, [r12, #864*4]       /* 13 */
    smlal r10, r11, r7, r4
    ldr r7, [r1, #128*4]        /*  2 */
    smlal r10, r11, r5, r3
    ldr r5, [r1, #224*4]        /*  3 */
    smlal r8, r9, r7, r3
    ldr r7, [r1, #256*4]        /*  4 */
    smlal r8, r9, r5, r4
    ldmia r2!, { r3-r4 }        /* load D[04..04] */
    ldr r5, [r1, #352*4]        /*  5 */
    smlal r8, r9, r7, r3
    ldr r7, [r12, #640*4]       /* 10 */
    smlal r8, r9, r5, r4
    ldr r5, [r12, #736*4]       /* 11 */
    smlal r10, r11, r7, r4
    ldr r7, [r1, #384*4]        /*  6 */
    smlal r10, r11, r5, r3
    ldmia r2!, { r3-r4 }        /* load D[06..07] */
    ldr r5, [r1, #480*4]        /*  7 */
    smlal r8, r9, r7, r3
    ldr r7, [r12, #512*4]       /*  8 */
    smlal r8, r9, r5, r4
    ldr r5, [r12, #608*4]       /*  9 */
    smlal r10, r11, r7, r4
    ldr r7, [r12, #384*4]       /*  6 */
    smlal r10, r11, r5, r3
    ldmia r2!, { r3-r4 }        /* load D[08..09] */
    ldr r5, [r12, #480*4]       /*  7 */
    smlal r10, r11, r7, r4
    ldr r7, [r1, #512*4]        /*  8 */
    smlal r10, r11, r5, r3
    ldr r5, [r1, #608*4]        /*  9 */
    smlal r8, r9, r7, r3
    ldr r7, [r1, #640*4]        /* 10 */
    smlal r8, r9, r5, r4
    ldmia r2!, { r3-r4 }        /* load D[10..11] */
    ldr r5, [r1, #736*4]        /* 11 */
    smlal r8, r9, r7, r3
    ldr r7, [r12, #256*4]       /*  4 */
    smlal r8, r9, r5, r4
    ldr r5, [r12, #352*4]       /*  5 */
    smlal r10, r11, r7, r4
    ldr r7, [r1, #768*4]        /* 12 */
    smlal r10, r11, r5, r3
    ldmia r2!, { r3-r4 }        /* load D[12..13] */
    ldr r5, [r1, #864*4]        /* 13 */
    smlal r8, r9, r7, r3
    ldr r7, [r12, #128*4]       /*  2 */
    smlal r8, r9, r5, r4
    ldr r5, [r12, #224*4]       /*  3 */
    smlal r10, r11, r7, r4
    ldr r7, [r12]               /*  0 */
    smlal r10, r11, r5, r3
    ldmia r2!, { r3-r4 }        /* load D[14..15] */
    ldr r5, [r12, #96*4]        /*  1 */
    smlal r10, r11, r7, r4
    ldr r7, [r1, #896*4]        /* 14 */
    smlal r10, r11, r5, r3
    ldr r5, [r1, #992*4]        /* 15 */
    smlal r8, r9, r7, r3
    smlal r8, r9, r5, r4
    /* store Data[01..15] */
    mov r8, r8, lsr #16
    orr r8, r8, r9, lsl #16     /* (lo>>16) || (hi<<16) */
    /* store Data[31..17] */
    mov r10, r10, lsr #16
    orr r10, r10, r11, lsl #16  /* (lo>>16) || (hi<<16) */
    rsb r10, r10, #0            /* r10 = -r10 */
    str r10, [r0, lr]           /* store Data */
    str r8, [r0], #4            /* store Data */
    /* correct adresses for next loop */
    sub r12, r12, #4            /* r12 = V-- */
    add r1, r1, #4              /* r1  = V++ */
    /* next loop */
    subs lr, lr, #8
    bgt .loop15
    
    /******************************************
     * V[16] with internal symmetry
     *****************************************/
    ldmia r2!, { r3-r6 }        /* load D[00..03] */
    ldr r7 , [r1]               /*  0 */
    ldr r10, [r1, #992*4]       /* 15 */
    ldr r11, [r1, #96*4]        /*  1 */
    ldr r12, [r1, #896*4]       /* 14 */
    rsb r10, r10, r7            /* V[00] - V[15] */
    smull r8, r9, r10, r3
    ldr r7 , [r1, #128*4]       /*  2 */
    ldr r10, [r1, #864*4]       /* 13 */
    rsb r12, r12, r11           /* V[01] - V[14] */
    smlal r8, r9, r12, r4
    ldr r11, [r1, #224*4]       /*  3 */
    ldr r12, [r1, #768*4]       /* 12 */
    rsb r10, r10, r7            /* V[02] - V[13] */
    smlal r8, r9, r10, r5
    ldr r7 , [r1, #256*4]       /*  4 */
    ldr r10, [r1, #736*4]       /* 11 */
    rsb r12, r12, r11           /* V[03] - V[12] */
    smlal r8, r9, r12, r6
    ldmia r2!, { r3-r6 }        /* load D[04..07] */
    ldr r11, [r1, #352*4]       /*  5 */
    ldr r12, [r1, #640*4]       /* 10 */
    rsb r10, r10, r7            /* V[04] - V[11] */
    smlal r8, r9, r10, r3
    ldr r7 , [r1, #384*4]       /*  6 */
    ldr r10, [r1, #608*4]       /*  9 */
    rsb r12, r12, r11           /* V[05] - V[10] */
    smlal r8, r9, r12, r4
    ldr r11, [r1, #480*4]       /*  7 */
    ldr r12, [r1, #512*4]       /*  8 */
    rsb r10, r10, r7            /* V[06] - V[09] */
    smlal r8, r9, r10, r5
    rsb r12, r12, r11           /* V[07] - V[08] */
    smlal r8, r9, r12, r6
    mov r8, r8, lsr #16
    orr r8, r8, r9, lsl #16     /* (lo>>16) || (hi<<16) */
    str r8, [r0], #4            /* store Data */
    
    ldmpc regs=r4-r11
#else
    mpc_decoder_windowing_D:
    /* r0 = Data[] */
    /* r1 = V[] */
    /* r2 = D[] */
    /* lr = counter */
    /************************************************************************
     * Further speed up through making use of symmetries within D[]-window.
     * The row V[00] can be extracted as it has symmetries within this single
     * row. 8 smull/mlal and 8 ldr's can be saved at the cost of 2 add's.
     * The rows V[01..15] are symmetric to V[31..17]. 15 x 16 ldr's can be
     * saved at the cost of 15 x 4 + 1 add's.
     * The row V[16] can be extracted as it has symmetries within this single
     * row. 8 smull/mlal and 8 ldr's can be saved.
     * On armv6 use smmulr/smlalr which are faster than smull/smlal and only
     * accumulate the top 32 bits of the result so that frees up 2
     * registers so we can ldm larger blocks.
     ***********************************************************************/
    stmfd   sp!, {r4-r11, lr}
    
    /******************************************
     * row 0 with internal symmetry
     *****************************************/
    add r2, r2, #4          /* D+=1, r2 = D[01] as D[00] = 0 */
    ldmia r2!, { r3-r6 }    /* load D[01..04] */
    ldr r7 , [r1, #96*4]    /*  1 */
    ldr r10, [r1, #992*4]   /* 15 */
    ldr r11, [r1, #128*4]   /*  2 */
    rsb r10, r10, r7        /* V[01] - V[15] */
    ldr r12, [r1, #896*4]   /* 14 */
    smmulr r9, r10, r3
    ldr r7 , [r1, #224*4]   /*  3 */
    add r12, r12, r11       /* V[02] + V[14] */
    ldr r10, [r1, #864*4]   /* 13 */
    smmlar r9, r12, r4, r9
    ldr r11, [r1, #256*4]   /*  4 */
    rsb r10, r10, r7        /* V[03] - V[13] */
    ldr r12, [r1, #768*4]   /* 12 */
    smmlar r9, r10, r5, r9
    ldr r7 , [r1, #352*4]   /*  5 */
    add r12, r12, r11       /* V[04] + V[12] */
    ldr r10, [r1, #736*4]   /* 11 */
    smmlar r9, r12, r6, r9
    ldmia r2!, { r3-r6 }    /* load D[05..08] */
    ldr r11, [r1, #384*4]   /*  6 */
    rsb r10, r10, r7        /* V[05] - V[11] */
    ldr r12, [r1, #640*4]   /* 10 */
    smmlar r9, r10, r3, r9
    ldr r7 , [r1, #480*4]   /*  7 */
    add r12, r12, r11       /* V[06] + V[10] */
    ldr r10, [r1, #608*4]   /*  9 */
    smmlar r9, r12, r4, r9
    rsb r10, r10, r7        /* V[07] - V[09] */
    ldr r11, [r1, #512*4]   /*  8 */
    smmlar r9, r10, r5, r9
    add r1, r1, #4          /* V+=1, r1 = V[01] */
    smmlar r9, r11, r6, r9
    add r2, r2, #7*4        /* D+=7, r2 = D[16] */
    mov r9, r9, lsl #2
    str r9, [r0], #4        /* store Data */

    /******************************************
     * rows 01..15 are symmetric to rows 31..17
     * r9  = acc of 01..15
     * r1  = V[01..15]
     * r11 = acc of 31..17
     * r12 = V[31..16]
     *****************************************/
    mov lr, #15*8
    add r12, r1, #30*4          /* r12 = V[31] */
.loop15:
    ldmia r2!, { r3-r6 }        /* load D[00..03] */
    ldr r7, [r12, #896*4]       /* 14 */
    ldr r8, [r12, #992*4]       /* 15 */
    smmulr r11, r7, r4
    ldr r7, [r1]                /*  0 */
    smmlar r11, r8, r3, r11
    ldr r8, [r1, #96*4]         /*  1 */
    smmulr r9, r7, r3
    ldr r7, [r12, #768*4]       /* 12 */
    smmlar r9, r8, r4, r9
    ldr r8, [r12, #864*4]       /* 13 */
    smmlar r11, r7, r6, r11
    ldr r7, [r1, #128*4]        /*  2 */
    smmlar r11, r8, r5, r11
    ldr r8, [r1, #224*4]        /*  3 */
    smmlar r9, r7, r5, r9
    ldr r7, [r1, #256*4]        /*  4 */
    smmlar r9, r8, r6, r9
    ldmia r2!, { r3-r6 }        /* load D[04..07] */
    ldr r8, [r1, #352*4]        /*  5 */
    smmlar r9, r7, r3, r9
    ldr r7, [r12, #640*4]       /* 10 */
    smmlar r9, r8, r4, r9
    ldr r8, [r12, #736*4]       /* 11 */
    smmlar r11, r7, r4, r11
    ldr r7, [r1, #384*4]        /*  6 */
    smmlar r11, r8, r3, r11
    ldr r8, [r1, #480*4]        /*  7 */
    smmlar r9, r7, r5, r9
    ldr r7, [r12, #512*4]       /*  8 */
    smmlar r9, r8, r6, r9
    ldr r8, [r12, #608*4]       /*  9 */
    smmlar r11, r7, r6, r11
    ldr r7, [r12, #384*4]       /*  6 */
    smmlar r11, r8, r5, r11
    ldmia r2!, { r3-r6 }        /* load D[08..11] */
    ldr r8, [r12, #480*4]       /*  7 */
    smmlar r11, r7, r4, r11
    ldr r7, [r1, #512*4]        /*  8 */
    smmlar r11, r8, r3, r11
    ldr r8, [r1, #608*4]        /*  9 */
    smmlar r9, r7, r3, r9
    ldr r7, [r1, #640*4]        /* 10 */
    smmlar r9, r8, r4, r9
    ldr r8, [r1, #736*4]        /* 11 */
    smmlar r9, r7, r5, r9
    ldr r7, [r12, #256*4]       /*  4 */
    smmlar r9, r8, r6, r9
    ldr r8, [r12, #352*4]       /*  5 */
    smmlar r11, r7, r6, r11
    ldr r7, [r1, #768*4]        /* 12 */
    smmlar r11, r8, r5, r11
    ldmia r2!, { r3-r6 }        /* load D[12..15] */
    ldr r8, [r1, #864*4]        /* 13 */
    smmlar r9, r7, r3, r9
    ldr r7, [r12, #128*4]       /*  2 */
    smmlar r9, r8, r4, r9
    ldr r8, [r12, #224*4]       /*  3 */
    smmlar r11, r7, r4, r11
    ldr r7, [r12]               /*  0 */
    smmlar r11, r8, r3, r11
    ldr r8, [r12, #96*4]        /*  1 */
    smmlar r11, r7, r6, r11
    ldr r7, [r1, #896*4]        /* 14 */
    smmlar r11, r8, r5, r11
    ldr r8, [r1, #992*4]        /* 15 */
    smmlar r9, r7, r5, r9
    sub r12, r12, #4            /* r12 = V-- correct adresses for next loop */
    smmlar r9, r8, r6, r9
    add r1, r1, #4              /* r1  = V++ correct adresses for next loop */
    rsb r11, r11, #0            /* r11 = -r11 */
    /* store Data[01..15] */
    mov r9, r9, lsl #2
    /* store Data[31..17] */
    mov r11, r11, lsl #2
    str r11, [r0, lr]           /* store Data */
    str r9, [r0], #4            /* store Data */
    /* next loop */
    subs lr, lr, #8
    bgt .loop15
    
    /******************************************
     * V[16] with internal symmetry
     *****************************************/
    ldmia r2!, { r3-r6 }        /* load D[00..03] */
    ldr r7 , [r1]               /*  0 */
    ldr r10, [r1, #992*4]       /* 15 */
    ldr r11, [r1, #96*4]        /*  1 */
    rsb r10, r10, r7            /* V[00] - V[15] */
    ldr r12, [r1, #896*4]       /* 14 */
    smmulr r9, r10, r3
    ldr r7 , [r1, #128*4]       /*  2 */
    rsb r12, r12, r11           /* V[01] - V[14] */
    ldr r10, [r1, #864*4]       /* 13 */
    smmlar r9, r12, r4, r9
    ldr r11, [r1, #224*4]       /*  3 */
    rsb r10, r10, r7            /* V[02] - V[13] */
    ldr r12, [r1, #768*4]       /* 12 */
    smmlar r9, r10, r5, r9
    ldr r7 , [r1, #256*4]       /*  4 */
    rsb r12, r12, r11           /* V[03] - V[12] */
    ldr r10, [r1, #736*4]       /* 11 */
    smmlar r9, r12, r6, r9
    ldmia r2!, { r3-r6 }        /* load D[04..07] */
    ldr r11, [r1, #352*4]       /*  5 */
    rsb r10, r10, r7            /* V[04] - V[11] */
    ldr r12, [r1, #640*4]       /* 10 */
    smmlar r9, r10, r3, r9
    ldr r7 , [r1, #384*4]       /*  6 */
    rsb r12, r12, r11           /* V[05] - V[10] */
    ldr r10, [r1, #608*4]       /*  9 */
    smmlar r9, r12, r4, r9
    ldr r11, [r1, #480*4]       /*  7 */
    rsb r10, r10, r7            /* V[06] - V[09] */
    ldr r12, [r1, #512*4]       /*  8 */
    smmlar r9, r10, r5, r9
    rsb r12, r12, r11           /* V[07] - V[08] */
    smmlar r9, r12, r6, r9
    mov r9, r9, lsl #2
    str r9, [r0], #4            /* store Data */
    
    ldmpc regs=r4-r11
#endif
.mpc_dewindowing_end:
    .size   mpc_decoder_windowing_D,.mpc_dewindowing_end-mpc_decoder_windowing_D
