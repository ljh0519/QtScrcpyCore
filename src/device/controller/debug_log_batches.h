#ifndef QTSCRCPY_DEBUG_LOG_BATCHES_H
#define QTSCRCPY_DEBUG_LOG_BATCHES_H

// Heisenbug elimination for commit 861be3f / submodule 2acb941 debug logs.
// Inventory: docs/debug-log-batches/batch{1,2,3,4}.txt
//
// QTSCRCPY_LOG_BATCH:
//   1..4  enable only that batch
//   0     all batches off
//  -1     all batches on
#ifndef QTSCRCPY_LOG_BATCH
#define QTSCRCPY_LOG_BATCH 4
#endif

#define QTSCRCPY_LOG_BATCH_ON(n) (((QTSCRCPY_LOG_BATCH) == (n)) || ((QTSCRCPY_LOG_BATCH) < 0))

#endif // QTSCRCPY_DEBUG_LOG_BATCHES_H
