// VMA implementation translation unit.
//
// VMA is header-only; it must be included with VMA_IMPLEMENTATION defined
// in exactly ONE .cpp file in the entire project. This is that file.
//
// Config:
//   VMA_RECORDING_ENABLED + VMA_STATS_STRING_ENABLED: set via vma_config
//     interface target (see fetch_third_party.cmake).
//   VMA_STATIC_VULKAN_FUNCTIONS=0: don't use static vkXxx() prototypes
//     (VK_NO_PROTOTYPES is defined project-wide via Volk).
//   VMA_DYNAMIC_VULKAN_FUNCTIONS=1: VMA loads functions itself via
//     vkGetInstanceProcAddr / vkGetDeviceProcAddr at vmaCreateAllocator.

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
