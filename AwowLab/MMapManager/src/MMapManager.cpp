#pragma once

#include <string>
#include <stdexcept>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// Read-only memory mapping of a whole file. Header-style class kept in a
// .cpp: users include this file directly.
class MMapFile {
public:
    MMapFile() = default;
    MMapFile(const MMapFile&) = delete;
    MMapFile& operator=(const MMapFile&) = delete;

    MMapFile(MMapFile&& other) noexcept { move_from(std::move(other)); }
    MMapFile& operator=(MMapFile&& other) noexcept {
        if (this != &other) {
            close();
            move_from(std::move(other));
        }
        return *this;
    }

    ~MMapFile() { close(); }

    void open(const std::string& filepath) {
        close();

#if defined(_WIN32) || defined(_WIN64)
        file_handle = CreateFileA(
            filepath.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE,  // Allow reading while WoW is writing
            nullptr,
            OPEN_EXISTING,
            // Logs are scanned front to back, so let the cache manager read ahead
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr
        );
        if (file_handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            throw std::runtime_error("Failed to open file: " + filepath + " (error code: " + std::to_string(error) + ")");
        }

        LARGE_INTEGER size;
        if (!GetFileSizeEx(file_handle, &size)) {
            close();
            throw std::runtime_error("Failed to get file size");
        }

        // An empty log has nothing to map (Windows refuses a mapping of
        // zero bytes); close it and leave the object with no data.
        if (size.QuadPart == 0) {
            close();
            return;
        }
        file_size = static_cast<size_t>(size.QuadPart);

        file_mapping_handle = CreateFileMappingA(
            file_handle,
            nullptr,
            PAGE_READONLY,
            0,
            0,
            nullptr
        );
        if (!file_mapping_handle) {
            close();
            throw std::runtime_error("Failed to create file mapping");
        }

        data = static_cast<const char*>(
            MapViewOfFile(
                file_mapping_handle,
                FILE_MAP_READ,
                0,
                0,
                0
            )
            );
        if (!data) {
            close();
            throw std::runtime_error("Failed to map view of file");
        }

        // Ask the memory manager to start paging the file in ahead of the
        // scan. Purely a hint - failure just means no head start.
        WIN32_MEMORY_RANGE_ENTRY range{ const_cast<char*>(data), file_size };
        PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
#else
        fd = ::open(filepath.c_str(), O_RDONLY);
        if (fd < 0) {
            throw std::runtime_error("Failed to open file");
        }

        off_t size = lseek(fd, 0, SEEK_END);
        if (size == -1) {
            close();
            throw std::runtime_error("Failed to seek file size");
        }
        if (size == 0) {
            close();
            return;
        }
        file_size = static_cast<size_t>(size);

        data = static_cast<const char*>(
            mmap(nullptr, file_size, PROT_READ, MAP_SHARED, fd, 0)
            );
        if (data == MAP_FAILED) {
            data = nullptr;
            close();
            throw std::runtime_error("Failed to mmap file");
        }

        // Logs are scanned front to back; tell the kernel to read ahead
        madvise(const_cast<char*>(data), file_size, MADV_SEQUENTIAL);
#endif
    }

    void close() {
#if defined(_WIN32) || defined(_WIN64)
        if (data) {
            UnmapViewOfFile(data);
            data = nullptr;
        }
        if (file_mapping_handle) {
            CloseHandle(file_mapping_handle);
            file_mapping_handle = nullptr;
        }
        if (file_handle != INVALID_HANDLE_VALUE) {
            CloseHandle(file_handle);
            file_handle = INVALID_HANDLE_VALUE;
        }
#else
        if (data) {
            munmap(const_cast<char*>(data), file_size);
            data = nullptr;
        }
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
#endif
        file_size = 0;
    }

    const char* get_data() const { return data; }
    size_t get_size() const { return file_size; }

private:
    void move_from(MMapFile&& other) {
        data = other.data;
        file_size = other.file_size;

#if defined(_WIN32) || defined(_WIN64)
        file_handle = other.file_handle;
        file_mapping_handle = other.file_mapping_handle;

        other.data = nullptr;
        other.file_size = 0;
        other.file_handle = INVALID_HANDLE_VALUE;
        other.file_mapping_handle = nullptr;
#else
        fd = other.fd;
        other.data = nullptr;
        other.file_size = 0;
        other.fd = -1;
#endif
    }

#if defined(_WIN32) || defined(_WIN64)
    HANDLE file_handle = INVALID_HANDLE_VALUE;
    HANDLE file_mapping_handle = nullptr;
#else
    int fd = -1;
#endif

    const char* data = nullptr;
    size_t file_size = 0;
};
