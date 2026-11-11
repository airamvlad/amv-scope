#pragma once

// A block of memory with a name, shared by every process of this user that
// opens the same name. New blocks start filled with zeros.
//
// If the system refuses (no permission, name taken by something else), the
// block is private to this process instead, so the plugin keeps working and
// only loses sight of instances in other processes.

#include <cstddef>
#include <string>

namespace amvscope
{

class SharedMemory
{
public:
    /** An empty name gives a private block. */
    SharedMemory (const std::string& name, std::size_t bytes);
    ~SharedMemory();

    SharedMemory (const SharedMemory&) = delete;
    SharedMemory& operator= (const SharedMemory&) = delete;

    void* data() const noexcept { return memory; }
    std::size_t size() const noexcept { return numBytes; }

    /** Forgets a name once no process needs it (only matters on macOS and Linux, where names outlive processes). */
    static void forget (const std::string& name);

    /** False: the block is private to this process. */
    bool isShared() const noexcept { return shared; }

private:
    void* memory = nullptr;
    std::size_t numBytes = 0;
    bool shared = false;
    void* handle = nullptr;   // Windows: the mapping handle
};

} // namespace amvscope
