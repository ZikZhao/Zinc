#pragma once
#include "pch.hpp"
#include <future>

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
        std::vector<std::size_t> line_offsets;
    };

public:
    FlatMap<GlobalMemory::String, std::uint32_t> file_id_map_;
    std::vector<File> files;

public:
    File* load(std::string_view input_path) noexcept {
        GlobalMemory::String path =
            std::filesystem::canonical(input_path)
                .string<char, std::char_traits<char>, GlobalMemory::String::allocator_type>();
        std::ifstream file_stream(input_path.data());
        if (file_stream.fail()) {
            return nullptr;
        }
        GlobalMemory::String content(
            (std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>()
        );
        std::vector<std::size_t> line_offsets = compute_line_offsets(content);
        files.push_back(
            File{
                .id = static_cast<std::uint32_t>(files.size()),
                .path = path,
                .content = std::move(content),
                .line_offsets = std::move(line_offsets)
            }
        );
        file_id_map_.insert({path, static_cast<std::uint32_t>(files.size()) - 1});
        return &files.back();
    }

    const File& operator[](std::size_t id) const noexcept {
        assert(id < file_id_map_.size());
        return files[id];
    }

private:
    std::vector<std::size_t> compute_line_offsets(std::string_view content) noexcept {
        std::vector<std::size_t> offsets;
        offsets.push_back(0);
        for (std::size_t i = 0; i < content.size(); ++i) {
            if (content[i] == '\n') {
                offsets.push_back(i + 1);
            }
        }
        return offsets;
    }
};

template <typename ResultType>
class ImportManager {
public:
    using CallbackType = std::function<ResultType(const SourceManager::File&)>;

private:
    SourceManager& sources_;
    std::mutex mutex_;
    FlatMap<std::string_view, std::shared_future<ResultType>> map_;

public:
    ImportManager(SourceManager& sources) noexcept : sources_(sources) {}

    std::shared_future<ResultType> import(std::string_view path, CallbackType callback) {
        std::lock_guard lock(mutex_);
        if (auto it = map_.find(path); it != map_.end()) {
            return it->second;
        } else {
            auto* file = sources_.load(path);
            auto import_future = std::async(std::launch::async, callback, std::ref(*file)).share();
            map_.insert({path, import_future});
            return map_.at(path);
        }
    }
};
