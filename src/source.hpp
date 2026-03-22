#pragma once
#include "pch.hpp"

#include "runtime-str.hpp"

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
        GlobalMemory::Vector<std::size_t> line_offsets;
    };

public:
    GlobalMemory::FlatMap<GlobalMemory::String, std::uint32_t> file_id_map_;
    GlobalMemory::Vector<File> files;
    GlobalMemory::FlatMap<std::uint32_t, const void*> node_cache_;

public:
    /// Used to load main file
    auto load(std::string_view input_path) noexcept -> std::uint32_t {
        assert(files.size() == 1 && file_id_map_.empty());
        GlobalMemory::String path =
            std::filesystem::canonical(input_path)
                .string<char, std::char_traits<char>, GlobalMemory::String::allocator_type>();
        std::ifstream file_stream(path.c_str());
        if (file_stream.fail()) {
            return std::numeric_limits<std::uint32_t>::max();
        }
        GlobalMemory::String content(
            (std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>()
        );
        GlobalMemory::Vector<std::size_t> line_offsets = compute_line_offsets(content);
        files.push_back(
            File{
                .id = static_cast<std::uint32_t>(files.size()),
                .path = path,
                .content = std::move(content),
                .line_offsets = std::move(line_offsets)
            }
        );
        file_id_map_.insert({path, static_cast<std::uint32_t>(files.size()) - 1});
        return static_cast<std::uint32_t>(files.size()) - 1;
    }

    auto load(std::string_view module_path, std::uint32_t relative_to) noexcept -> std::uint32_t {
        assert(relative_to < files.size());
        std::filesystem::path base_path =
            std::filesystem::path(files[relative_to].path.c_str()).parent_path();
        std::filesystem::path full_path = base_path / module_path;
        auto [it, inserted] = file_id_map_.insert(
            {full_path.string<char, std::char_traits<char>, GlobalMemory::String::allocator_type>(),
             0}
        );
        if (!inserted) {
            return it->second;
        }
        std::ifstream file_stream(full_path.c_str());
        if (file_stream.fail()) {
            return std::numeric_limits<std::uint32_t>::max();
        }
        GlobalMemory::String content(
            (std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>()
        );
        GlobalMemory::Vector<std::size_t> line_offsets = compute_line_offsets(content);
        files.push_back(
            File{
                .id = static_cast<std::uint32_t>(files.size()),
                .path = full_path.string<
                    char,
                    std::char_traits<char>,
                    GlobalMemory::String::allocator_type>(),
                .content = std::move(content),
                .line_offsets = std::move(line_offsets)
            }
        );
        it->second = static_cast<std::uint32_t>(files.size()) - 1;
        return it->second;
    }

    auto load_std() noexcept -> std::uint32_t {
        GlobalMemory::String content(std_d_zn_str());
        GlobalMemory::Vector<std::size_t> line_offsets = compute_line_offsets(content);
        assert(files.empty());
        files.push_back(
            File{
                .id = std::numeric_limits<std::uint32_t>::max(),
                .path = "<std.zinc>",
                .content = std::move(content),
                .line_offsets = std::move(line_offsets)
            }
        );
        return 0;
    }

    auto operator[](std::size_t id) const noexcept -> const File& {
        assert(id < files.size());
        return files[id];
    }

    auto set_cache(std::uint32_t file_id, const void* node) noexcept -> void {
        node_cache_[file_id] = node;
    }

    auto get_cache(std::uint32_t file_id) const noexcept -> const void* {
        auto it = node_cache_.find(file_id);
        if (it != node_cache_.end()) {
            return it->second;
        } else {
            return nullptr;
        }
    }

private:
    auto compute_line_offsets(std::string_view content) noexcept
        -> GlobalMemory::Vector<std::size_t> {
        GlobalMemory::Vector<std::size_t> offsets;
        offsets.push_back(0);
        for (std::size_t i = 0; i < content.size(); ++i) {
            if (content[i] == '\n') {
                offsets.push_back(i + 1);
            }
        }
        return offsets;
    }
};
