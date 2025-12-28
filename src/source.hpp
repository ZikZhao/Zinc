#pragma once
#include "pch.hpp"

class FileNotFoundError final : public std::runtime_error {
public:
    FileNotFoundError(std::string_view filename)
        : std::runtime_error(std::format("File not found: {}", filename)) {}
};

struct Location {
    std::uint32_t id = 0;
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

class SourceManager {
public:
    struct File {
        std::uint32_t id;
        GlobalMemory::String path;
        GlobalMemory::String content;
    };

public:
    FlatMap<GlobalMemory::String, std::uint32_t> file_id_map_;
    std::vector<File> files;

public:
    SourceManager() = default;
    std::optional<std::uint32_t> load(std::string_view input_path) noexcept {
        GlobalMemory::String path =
            std::filesystem::canonical(input_path)
                .string<char, std::char_traits<char>, GlobalMemory::String::allocator_type>();
        std::ifstream file_stream(input_path.data());
        if (file_stream.fail()) {
            return std::nullopt;
        }
        GlobalMemory::String content(
            (std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>()
        );
        files.push_back(
            File{
                .id = static_cast<std::uint32_t>(files.size()),
                .path = path,
                .content = std::move(content)
            }
        );
        file_id_map_.insert(path, static_cast<std::uint32_t>(files.size()) - 1);
        return files.back().id;
    }
    const File& operator[](std::size_t id) const noexcept {
        assert(id < file_id_map_.size());
        return files[id];
    }
};
