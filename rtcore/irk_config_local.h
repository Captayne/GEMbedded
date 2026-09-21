/*
 * irk_config_local.h - IRKernel configuration for the pTOS real-time core
 *
 * One kernel for both cores: pTOS is the main task of core 0, the
 * runtime's mailbox loop that of core 1.  Task stacks come from the
 * runtime's own pool, never from malloc().
 */

#define IRK_MAX_CORES            2
#define IRK_MAX_TASKS            16
#define IRK_ENABLE_MALLOC_STACKS 0
#define IRK_ENABLE_NAMES         1
#define IRK_ENABLE_STATS         1
#define IRK_ENABLE_STACKCHECK    1
