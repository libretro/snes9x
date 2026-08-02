#ifndef _SA1HW_H_
#define _SA1HW_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t  *SA1FillRAM;
extern uint8_t  *SA1BWRAMBase;
extern uint32_t  SA1BWRAMMask;

int     S9xSA1BWRAMWriteProtected (uint32_t bwoffset);
uint8_t S9xSA1ReadCC1 (uint32_t bwoffset);

#ifdef __cplusplus
}
#endif

#endif
