#ifndef TCM_SECTIONS_H_
#define TCM_SECTIONS_H_

#include "bsp_api.h"

#define TCM_ITCM_CODE       BSP_PLACE_IN_SECTION(".itcm_code_from_flash")
#define TCM_DTCM_DATA       BSP_PLACE_IN_SECTION(".dtcm")
#define TCM_DTCM_NOINIT     BSP_PLACE_IN_SECTION(".dtcm_noinit")

#endif
