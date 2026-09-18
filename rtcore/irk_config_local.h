/*
 * irk_config_local.h - IRKernel configuration for the pTOS real-time core
 *
 * Only core 1 runs IRKernel (core 0 belongs to pTOS), so as far as the
 * kernel is concerned this is a single core machine.  Task stacks come
 * from the runtime's own pool, never from malloc().
 */

#define IRK_MAX_CORES            1
#define IRK_MAX_TASKS            10
#define IRK_ENABLE_MALLOC_STACKS 0
#define IRK_ENABLE_NAMES         1
#define IRK_ENABLE_STATS         1
#define IRK_ENABLE_STACKCHECK    1
