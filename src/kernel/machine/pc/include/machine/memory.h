#pragma once
#define KERNEL_LOAD_OFFSET 0x400000
#define KERNEL_PHYSICAL_BASE 0x0
#define KERNEL_VIRTUAL_BASE 0xFFFFFFFF80000000
#define PHYSICAL_MAP_START 0xFFFFFFFFC0000000
#define MEMORY_BOOTSTRAP_MAX (1024 * 1024 * 1024)

/* 131072 */
/* kernel_virtual_base: 131071 in kernel space */
