#include "mem_backend.h"

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <sys/mman.h>
  #include <unistd.h>
#endif

static size_t g_page_size = 0;
static size_t g_allocated_ever = 0;

size_t mem_backend_page_size(void) {
    if (g_page_size == 0) {
#if defined(_WIN32)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        g_page_size = si.dwPageSize;
#else
        long v = sysconf(_SC_PAGESIZE);
        g_page_size = (v > 0) ? (size_t)v : 4096;
#endif
    }
    return g_page_size;
}

void *mem_backend_alloc(size_t bytes) {
    if (bytes == 0)
        bytes = mem_backend_page_size();

#if defined(_WIN32)
    void *p = VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    void *p = mmap(NULL, bytes, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED)
        p = NULL;
#endif

    if (p)
        g_allocated_ever += bytes;
    return p;
}

void mem_backend_free(void *p, size_t bytes) {
    if (!p)
        return;
#if defined(_WIN32)
    (void)bytes;
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, bytes);
#endif
}

size_t mem_backend_allocated_ever(void) {
    return g_allocated_ever;
}
