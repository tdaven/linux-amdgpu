/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

/**
 * @file   drDrmApi.h
 * @brief  Contains DCI data structure
 *
 */

#ifndef __DRDRMAPI_H__
#define __DRDRMAPI_H__

#define DRM_PSTORAGE_SIZE           (1024*64)

// Note: DCI buffer consists of two parts. First 4 KB is used for DCI interface,
//       remaining part contains Persistent Storage data.
//
// DRM DCI message
typedef struct 
{
    union {
        unsigned char               Buffer0[1024*2];     // 2 KB padding
    };
    union {
        unsigned char               Buffer1[1024*2];     // 2 KB padding
    };
    unsigned char       PStorage[DRM_PSTORAGE_SIZE];    // Persistent Storage buffer

} dciMessage_t;

#endif // __DRDRMAPI_H__
