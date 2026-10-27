/* SoftFloat platform settings for MSVC on x86-64 (replaces the GCC-only
   build/Win64-MinGW-w64/platform.h). No GCC builtins, no __int128. */
#define LITTLEENDIAN 1
#define INLINE static __inline
