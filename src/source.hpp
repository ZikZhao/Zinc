#pragma once
#include "pch.hpp"

#include "runtime-str.hpp"

struct Location {
    std::uint32_t id = 0;
    std::uint32_t begin = 0;
    std::uint32_t end = 0;
};

class SourceFile {
public:
    std::filesystem::path path_;
    GlobalMemory::String content_;

private:
    GlobalMemory::Vector<std::size_t> line_offsets_;
    GlobalMemory::Vector<std::size_t> byte_offsets_;

public:
    SourceFile(std::filesystem::path path, GlobalMemory::String content) noexcept
        : path_(std::move(path)), content_(std::move(content)) {
        compute_offsets();
    }

    auto to_byte_index(std::size_t char_index) const noexcept -> std::size_t {
        auto it = std::lower_bound(byte_offsets_.begin(), byte_offsets_.end(), char_index);
        return char_index + static_cast<std::size_t>(std::distance(byte_offsets_.begin(), it));
    }

    auto get_line_number(std::size_t byte_index) const noexcept -> std::int64_t {
        auto it = std::upper_bound(line_offsets_.begin(), line_offsets_.end(), byte_index);
        return static_cast<std::int64_t>(std::distance(line_offsets_.begin(), it));
    }

private:
    auto compute_offsets() noexcept -> void {
        line_offsets_.reserve(content_.size() / 100);
        line_offsets_.push_back(0);
        for (std::size_t i = 0; i < content_.size(); ++i) {
            if (content_[i] == '\n') {
                line_offsets_.push_back(i + 1);
            }
            int width = std::countl_one(static_cast<std::uint8_t>(content_[i]));
            if (width == 2) {
                byte_offsets_.push_back(i);
            } else if (width == 3) {
                byte_offsets_.push_back(i);
                byte_offsets_.push_back(i);
            } else if (width == 4) {
                byte_offsets_.push_back(i);
                byte_offsets_.push_back(i);
                byte_offsets_.push_back(i);
            }
        }
    }
};

class SourceManager {
public:
    GlobalMemory::FlatMap<std::filesystem::path, std::uint32_t> file_id_map_;
    GlobalMemory::Vector<SourceFile> files;
    GlobalMemory::FlatMap<std::uint32_t, const void*> node_cache_;

public:
    /// Used to load main file
    auto load(strview input_path) noexcept -> std::uint32_t {
        assert(files.size() == 1 && file_id_map_.empty());
        std::filesystem::path path = std::filesystem::canonical(input_path);
        std::ifstream file_stream(path.c_str());
        if (file_stream.fail()) {
            return std::numeric_limits<std::uint32_t>::max();
        }
        GlobalMemory::String content(
            (std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>()
        );
        files.emplace_back(path, std::move(content));
        file_id_map_.insert({std::move(path), static_cast<std::uint32_t>(files.size()) - 1});
        return static_cast<std::uint32_t>(files.size()) - 1;
    }

    auto load(strview module_path, std::uint32_t relative_to) noexcept -> std::uint32_t {
        assert(relative_to < files.size());
        std::filesystem::path base_path =
            std::filesystem::path(files[relative_to].path_.c_str()).parent_path();
        std::filesystem::path full_path = base_path / module_path;
        auto [it, inserted] = file_id_map_.insert({full_path, 0});
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
        files.emplace_back(full_path, std::move(content));
        it->second = static_cast<std::uint32_t>(files.size()) - 1;
        return it->second;
    }

    auto load_std() noexcept -> std::uint32_t {
        GlobalMemory::String content(std_d_zn_str());
        assert(files.empty());
        files.emplace_back("", std::move(content));
        return 0;
    }

    auto operator[](std::size_t id) const noexcept -> const SourceFile& {
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
};
