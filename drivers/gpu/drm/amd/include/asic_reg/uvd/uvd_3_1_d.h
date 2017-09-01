/*
 *
 * Copyright (C) 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef UVD_3_1_D_H
#define UVD_3_1_D_H

#define ixUVD_CGC_CTRL2                           0x00C1
#define ixUVD_CGC_MEM_CTRL                        0x00C0

#define mmUVD_CGC_CTRL                            0x3D2C
#define mmUVD_CGC_GATE                            0x3D2A

#define mmUVD_CONTEXT_ID                          0x3DBD
#define mmUVD_CTX_DATA                            0x3D29
#define mmUVD_CTX_INDEX                           0x3D28

#define mmUVD_GPCOM_VCPU_CMD                      0x3BC3
#define mmUVD_GPCOM_VCPU_DATA0                    0x3BC4
#define mmUVD_GPCOM_VCPU_DATA1                    0x3BC5

#define mmUVD_RB_ARB_CTRL                         0x3D20

#define mmUVD_LMI_ADDR_EXT                        0x3D65
#define mmUVD_LMI_CTRL                            0x3D66
#define mmUVD_LMI_CTRL2                           0x3D3D
#define mmUVD_LMI_EXT40_ADDR                      0x3D26

#define mmUVD_LMI_SWAP_CNTL                       0x3D6D
#define mmUVD_MASTINT_EN                          0x3D40

#define mmUVD_MPC_SET_ALU                         0x3D7E
#define mmUVD_MPC_SET_MUX                         0x3D7D
#define mmUVD_MPC_SET_MUXA0                       0x3D79
#define mmUVD_MPC_SET_MUXA1                       0x3D7A
#define mmUVD_MPC_SET_MUXB0                       0x3D7B
#define mmUVD_MPC_SET_MUXB1                       0x3D7C
#define mmUVD_MP_SWAP_CNTL                        0x3D6F
#define mmUVD_NO_OP                               0x3BFF

#define mmUVD_RBC_IB_BASE                         0x3DA1
#define mmUVD_RBC_IB_SIZE                         0x3DA2

#define mmUVD_RBC_RB_BASE                         0x3DA3
#define mmUVD_RBC_RB_CNTL                         0x3DA9
#define mmUVD_RBC_RB_RPTR                         0x3DA4

#define mmUVD_RBC_RB_WPTR                         0x3DA5
#define mmUVD_RBC_RB_WPTR_CNTL                    0x3DA6

#define mmUVD_SEMA_CNTL                           0x3D00
#define mmUVD_SEMA_SIGNAL_INCOMPLETE_TIMEOUT_CNTL 0x3DB3
#define mmUVD_SEMA_TIMEOUT_STATUS                 0x3DB0
#define mmUVD_SEMA_WAIT_FAULT_TIMEOUT_CNTL        0x3DB2
#define mmUVD_SEMA_WAIT_INCOMPLETE_TIMEOUT_CNTL   0x3DB1

#define mmUVD_SOFT_RESET                          0x3DA0
#define mmUVD_STATUS                              0x3DAF

#define mmUVD_UDEC_ADDR_CONFIG                    0x3BD3
#define mmUVD_UDEC_DB_ADDR_CONFIG                 0x3BD4
#define mmUVD_UDEC_DBW_ADDR_CONFIG                0x3BD5

#define mmUVD_VCPU_CHIP_ID				          0x3D35
#define mmUVD_VCPU_CACHE_OFFSET0                  0x3D36
#define mmUVD_VCPU_CACHE_OFFSET1                  0x3D38
#define mmUVD_VCPU_CACHE_OFFSET2                  0x3D3A
#define mmUVD_VCPU_CACHE_SIZE0                    0x3D37
#define mmUVD_VCPU_CACHE_SIZE1                    0x3D39
#define mmUVD_VCPU_CACHE_SIZE2                    0x3D3B
#define mmUVD_VCPU_CNTL                           0x3D98

#endif
