#ifndef _PROTOCOL_606TX_H_
#define _PROTOCOL_606TX_H_

#include "../protocol.h"

#define C2F(c) (c * 1.8 + 32)
#define F2C(f) (f - 32 / 1.8)

struct protocol_t *acurite_606tx;
void acurite606TXInit(void);

#endif
