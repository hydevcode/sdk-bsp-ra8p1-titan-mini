/*
 * This file is developed by EdgeCortix Inc. to be used with certain Renesas Electronics Hardware only.
 *
 * Copyright © 2025 EdgeCortix Inc. Licensed to Renesas Electronics Corporation with the
 * right to sublicense under the Apache License, Version 2.0.
 *
 * This file also includes source code originally developed by the Renesas Electronics Corporation.
 * The Renesas disclaimer below applies to any Renesas-originated portions for usage of the code.
 *
 * The Renesas Electronics Corporation
 * DISCLAIMER
 * This software is supplied by Renesas Electronics Corporation and is only intended for use with Renesas products. No
 * other uses are authorized. This software is owned by Renesas Electronics Corporation and is protected under all
 * applicable laws, including copyright laws.
 * THIS SOFTWARE IS PROVIDED 'AS IS' AND RENESAS MAKES NO WARRANTIES REGARDING
 * THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED. TO THE MAXIMUM
 * EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES
 * SHALL BE LIABLE FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR ANY REASON RELATED TO THIS
 * SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
 * Renesas reserves the right, without notice, to make changes to this software and to discontinue the availability of
 * this software. By using this software, you agree to the additional terms and conditions found by accessing the
 * following link:
 * http://www.renesas.com/disclaimer
 *
 * Changed from original python code to C source code.
 * Copyright (C) 2017 Renesas Electronics Corporation. All rights reserved.
 *
 * This file also includes source codes originally developed by the TensorFlow Authors which were distributed under the following conditions.
 *
 * The TensorFlow Authors
 * Copyright 2023 The Apache Software Foundation
 *
 * This product includes software developed at
 * The Apache Software Foundation (http://www.apache.org/).
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "sub_0000_io_data.h"

// Input tensor: serving_default_image_0
// Data Type: INT8
// Shape: [1, 28, 28, 1]
// Number of elements: 784
// Size in bytes: 784
int8_t sub_0000_serving_default_image_0[] = {
  12, 23, 55, 88, 26, 91, 11, 88, -20, 31,
  37, -30, -16, -52, 100, -114, 118, -59, -30, -6,
  74, 79, 7, -6, 17, -28, 108, 86, -110, -42,
  -106, 37, -123, -34, 85, 117, 71, -93, 94, 94,
  122, -7, 76, 77, -10, 5, 71, 45, -98, 56,
  35, 20, -92, 9, 113, 66, 5, -101, -22, -7,
  -61, -81, 70, 60, -12, -73, 17, -94, -124, -46,
  30, -90, 28, -72, 29, -30, 113, 103, 46, -13,
  -36, 28, -17, 103, 50, -103, -113, 120, 42, 39,
  43, -85, -75, -37, -95, 64, -48, 27, -35, -45,
  17, -119, -16, 34, 125, 117, -102, 39, -75, 34,
  -87, 126, 39, 20, -64, -22, -9, -7, -66, 31,
  -88, -42, -100, 44, 40, -47, -93, 71, -78, 115,
  -34, 41, 82, -125, -104, 31, 86, 44, -104, 120,
  121, 96, -9, 2, 122, -114, 26, -13, 61, -123,
  -118, -15, -56, 122, -98, -36, -53, -5, -98, 48,
  -47, 97, -22, 107, -112, -73, 49, 16, 17, 93,
  -61, 2, 5, 106, -104, 107, 19, -107, 109, -57,
  -47, -126, 42, 87, -95, 37, 55, 87, -54, -61,
  -82, -27, 22, 13, -123, -86, 84, -34, -127, -91,
  45, 17, -59, 52, 60, -55, 118, -18, -65, 65,
  19, -27, 23, 101, 18, 35, -71, 100, 115, 46,
  -14, -14, 88, 122, 51, -99, -52, 68, 80, -23,
  -27, 44, 97, -65, 20, -48, 97, 119, 49, 22,
  57, 40, 0, 8, 116, -69, 36, -27, -20, 30,
  27, -7, -124, -8, -51, 55, 41, -55, -54, -30,
  30, 63, -19, 96, -94, -102, -52, -105, 17, -38,
  23, 13, 19, -120, 39, 120, 38, -46, -18, -72,
  101, -92, -34, -104, -17, 123, 100, -62, 78, 9,
  52, -14, -103, -103, 107, -38, 54, -8, 127, 87,
  -90, 103, 94, -119, -87, 2, 29, -86, -97, 71,
  89, 93, 78, -23, 17, -93, -24, -120, -111, 123,
  50, -33, -12, -21, 56, -116, 93, -35, 121, -124,
  91, -69, -126, 67, -36, 113, 58, 63, -85, -42,
  5, -3, -115, -42, -77, -83, -124, -85, 75, -10,
  -71, 95, -40, 113, 109, 27, 52, 24, -120, 72,
  -86, 0, 31, -116, 19, 50, -68, 126, 111, -60,
  29, 45, 9, 93, 23, 64, 58, 118, -49, 13,
  -27, -74, -75, -72, -81, -73, 113, 17, 61, -13,
  -3, 120, -70, 46, -63, -107, -114, -114, -17, -4,
  -49, 97, 50, 121, -32, 30, -83, 10, -122, 90,
  -111, 62, 45, -6, -12, 45, 9, 27, 101, 54,
  125, -8, -73, -12, 41, 104, -61, -93, -123, -70,
  66, 97, -47, 103, -30, 37, 22, -45, 84, 5,
  33, -128, 95, -49, -58, -20, 76, 98, -81, 46,
  115, -12, 47, -5, -73, 73, 114, -70, 59, 97,
  -63, -48, -74, 117, 4, -8, -122, 54, -75, -89,
  -20, 58, -33, 37, -10, -73, -57, -81, 22, 78,
  93, 63, -98, 44, 4, -58, -95, -84, 55, 52,
  -27, -10, 16, 87, -82, -76, -91, -86, -4, -97,
  -37, 56, 112, -121, 67, 63, 63, -105, 103, -73,
  -107, 63, 13, 59, 21, -117, 118, -75, -54, -55,
  -67, 45, -103, -112, -124, 14, 109, -126, 43, 85,
  72, 123, -56, 52, 22, -82, -112, 3, -4, 20,
  122, 73, 96, 27, -42, -73, 118, -12, -69, 96,
  115, -2, 112, 55, 76, -4, 33, 53, 95, -1,
  -53, 88, 89, -79, 30, 69, -125, 121, -40, 92,
  -91, 71, 123, 124, -6, 64, -1, -127, 35, -60,
  -34, -23, -93, -19, 82, -52, -80, -26, 2, -98,
  -71, 123, -103, -25, 92, 17, 121, -41, 117, 73,
  104, -23, 70, -37, -43, -26, -108, -51, -24, 70,
  -69, 109, -95, -45, -115, 115, 57, -125, -126, 8,
  69, -51, -91, 98, -108, -64, -106, 45, 44, 79,
  -66, -18, -21, 64, 14, 84, 92, -31, 58, -104,
  -59, -63, -95, 23, -114, -7, -51, -4, -61, -11,
  -12, 6, 46, -15, 50, 90, -56, -18, -31, 83,
  -82, 2, 73, -106, -114, 40, 50, -76, 71, 88,
  71, 46, -62, -83, -33, -111, 22, -126, -59, 100,
  -34, -94, -78, 71, -11, 108, -117, 55, 76, -3,
  -109, -8, 4, 97, -50, -3, 19, -22, 117, -84,
  37, -7, -119, 72, -18, 14, 2, -88, 9, -92,
  46, 38, -57, 10, -96, -45, -28, -91, 116, -88,
  -81, -95, 103, 20, 11, -105, -12, 105, 97, -107,
  -11, 76, 57, 96, -26, -105, 103, -19, 48, -7,
  51, 20, -45, 55, 65, -122, 34, 59, -67, 68,
  -87, -126, 75, -50, 117, -69, -11, 0, 23, 116,
  91, 14, -11, -104, 115, 31, 19, 92, 82, -88,
  104, 126, 80, -107, -88, -104, 32, 34, -27, 115,
  -112, 111, -20, -14,
};

// Output tensor: StatefulPartitionedCall_0_70015
// Data Type: INT8
// Shape: [1, 10]
// Number of elements: 10
// Size in bytes: 10
int8_t sub_0000_StatefulPartitionedCall_0_70015[] = {
  -2, -20, -6, -6, -32, -11, -12, -42, 20, -24,
};

