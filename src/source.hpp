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
public:
    FlatMap<std::string, std::string> files_;
    std::vector<std::string> file_order_;

public:
    SourceManager() = default;
    auto load(std::string_view filename) noexcept {
        struct SourceFile {
            std::string path;
            const std::string* content;
        };
        std::string absolute_path = std::filesystem::canonical(filename).string();
        std::ifstream file_stream(filename.data());
        if (file_stream.fail()) {
            return SourceFile{.path = "", .content = nullptr};
        }
        std::string content(
            (std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>()
        );
        const std::string& file_content = (files_[absolute_path] = std::move(content));
        file_order_.push_back(absolute_path);
        return SourceFile{.path = absolute_path, .content = &file_content};
    }
    const std::string& operator[](std::size_t id) const noexcept {
        assert(id < file_order_.size());
        return files_.at(file_order_.at(id));
    }
    std::uint32_t get_file_id(std::string filename) const noexcept {
        for (std::uint32_t i = 0; i < file_order_.size(); ++i) {
            if (file_order_[i] == filename) {
                return i;
            }
        }
        assert(false && "File not found in SourceManager");
    }
};
