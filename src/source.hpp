#pragma once
#include "pch.hpp"

struct Location {
    std::size_t id = 0uz;
    struct {
        std::size_t line;
        std::size_t column;
    } begin = {0, 0}, end = {0, 0};
};

class SourceManager {
public:
    std::map<std::string, std::string> files_;
    std::vector<std::string> file_order_;

public:
    SourceManager() = default;
    auto operator[](std::string_view filename) {
        struct SourceFile {
            std::string path;
            const std::string& content;
        };
        std::ifstream file_stream(filename.data());
        if (file_stream.fail()) {
            throw std::runtime_error("Cannot open source file: "s + filename.data());
        }
        std::string absolute_path = std::filesystem::canonical(filename).string();
        std::string content(
            (std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>()
        );
        const std::string& file_content = (files_[absolute_path] = std::move(content));
        file_order_.push_back(absolute_path);
        return SourceFile{.path = absolute_path, .content = file_content};
    }
    const std::string& operator[](std::size_t index) const noexcept {
        assert(index < file_order_.size());
        return files_.at(file_order_.at(index));
    }
    std::size_t index(std::string filename) const noexcept {
        for (std::size_t i = 0; i < file_order_.size(); ++i) {
            if (file_order_[i] == filename) {
                return i;
            }
        }
        assert(false && "File not found in SourceManager");
    }
};
