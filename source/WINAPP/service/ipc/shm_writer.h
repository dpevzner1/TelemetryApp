#pragma once
#include "../../shared/shm_layout.h"

namespace Service {

bool     ShmOpen();
void     ShmClose();
ShmBlock* ShmGet();   // returns nullptr if not open

// Seqlock helpers — wrap all metric writes between these calls
void ShmBeginWrite(ShmBlock* b);
void ShmEndWrite(ShmBlock* b);
void ShmPush(ShmBlock* b, uint32_t metric_id, double value);

} // namespace Service
