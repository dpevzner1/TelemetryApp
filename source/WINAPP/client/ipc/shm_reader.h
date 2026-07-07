#pragma once
#include "../../shared/shm_layout.h"
#include <cstdint>

namespace Client {

// Maps the service's shared memory block read-only.
// Call ShmReaderOpen() once at startup; ShmReaderGet() on each frame.
bool      ShmReaderOpen();
void      ShmReaderClose();
const ShmBlock* ShmReaderGet();  // nullptr if not mapped

// Read current value for one metric with seqlock safety.
// Returns false if a consistent read could not be obtained after retries.
bool ShmReadMetric(uint32_t metric_id, double& out);

// Read last N history values. Returns count actually read.
uint32_t ShmReadHistory(uint32_t metric_id, double* out, uint32_t n);

} // namespace Client
