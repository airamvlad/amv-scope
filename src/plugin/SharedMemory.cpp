#include "SharedMemory.h"

#include <cstdlib>
#include <cstring>

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace amvscope
{

SharedMemory::SharedMemory (const std::string& name, std::size_t bytes) : numBytes (bytes)
{
    if (name.empty())
    {
        memory = std::calloc (1, bytes);
        return;
    }

   #if defined (_WIN32)
    // "Local\": visible to the processes of this login session.
    const std::string fullName = "Local\\" + name;
    const auto size = (unsigned long long) bytes;

    if (HANDLE mapping = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                             (DWORD) (size >> 32), (DWORD) (size & 0xffffffffull), fullName.c_str()))
    {
        if (void* view = MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes))
        {
            memory = view;
            handle = mapping;
            shared = true;
        }
        else
        {
            CloseHandle (mapping);
        }
    }
   #else
    // POSIX names start with a slash; macOS allows at most 31 characters.
    const std::string fullName = "/" + name;
    const int fd = shm_open (fullName.c_str(), O_CREAT | O_RDWR, 0600);

    if (fd >= 0)
    {
        const auto bigEnough = [&]
        {
            struct stat info {};
            return fstat (fd, &info) == 0 && (std::size_t) info.st_size >= bytes;
        };

        // Only the first process sizes the block; macOS refuses to resize it later.
        // (If sizing fails, the check below sees it.)
        const bool sized = bigEnough() || ftruncate (fd, (off_t) bytes) == 0;

        if (sized && bigEnough())
        {
            void* view = mmap (nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

            if (view != MAP_FAILED)
            {
                memory = view;
                shared = true;
            }
        }

        close (fd);
    }
   #endif

    if (memory == nullptr)
        memory = std::calloc (1, bytes);
}

void SharedMemory::forget (const std::string& name)
{
   #if defined (_WIN32)
    (void) name;   // Windows drops the block with its last user
   #else
    shm_unlink (("/" + name).c_str());
   #endif
}

SharedMemory::~SharedMemory()
{
    if (! shared)
    {
        std::free (memory);
        return;
    }

   #if defined (_WIN32)
    UnmapViewOfFile (memory);
    CloseHandle ((HANDLE) handle);
   #else
    munmap (memory, numBytes);
   #endif
}

} // namespace amvscope
