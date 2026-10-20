// Standalone host-only checks do not boot an Xbox image.
#include <cstdint>
namespace kernel { uint8_t* g_base = nullptr; }
