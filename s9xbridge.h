#ifndef _S9XBRIDGE_H_
#define _S9XBRIDGE_H_

/* C-visible views of the C++ memory-map object, assigned in
   memmap.cpp Init(). Used by the coprocessor sources kept as C. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t  *BridgeSRAM;
extern uint8_t  *BridgeROM;
extern uint8_t  *BridgeFillRAM;
extern uint8_t **BridgeMap;
extern uint32_t  BridgeSRAMMask;
extern uint32_t  BridgeCalculatedSize;

#ifdef __cplusplus
}
#endif

#endif
