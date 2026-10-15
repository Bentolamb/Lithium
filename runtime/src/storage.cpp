#include "lithium/storage.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace lithium {

std::string encodeStorageName(std::string_view name) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::string decodeStorageName(std::string_view e) {
    auto val = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
    std::string out;
    for (std::size_t i = 0; i < e.size(); i++) {
        if (e[i] == '%' && i + 2 < e.size() + 0 && i + 2 <= e.size() - 1 + 0) {
            out.push_back(static_cast<char>(val(e[i + 1]) * 16 + val(e[i + 2])));
            i += 2;
        } else {
            out.push_back(e[i]);
        }
    }
    return out;
}

bool MemoryStorage::listNames(const std::string& area, std::vector<std::string>& out) {
    for (const auto& kv : files_) if (kv.first.first == area) out.push_back(kv.first.second);
    return true;
}

bool FileStorage::listNames(const std::string& area, std::vector<std::string>& out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(fs::path(root_) / encodeStorageName(area), ec)) {
        const std::string f = entry.path().filename().string();
        const std::string ext = ".outlist";
        if (f.size() > ext.size() && f.compare(f.size() - ext.size(), ext.size(), ext) == 0) {
            out.push_back(decodeStorageName(f.substr(0, f.size() - ext.size())));
        }
    }
    return true;
}

void MemoryStorage::submit(int id, StorageOp op, const std::string& area, const std::string& name, std::string data) {
    const auto key = std::make_pair(area, name);
    switch (op) {
        case StorageOp::Write: files_[key] = std::move(data); finish(id, StorageStatus::Ok); break;
        case StorageOp::Read: {
            const auto it = files_.find(key);
            if (it == files_.end()) finish(id, StorageStatus::NotFound);
            else finish(id, StorageStatus::Ok, it->second);
            break;
        }
        case StorageOp::Exists: finish(id, files_.count(key) ? StorageStatus::Ok : StorageStatus::NotFound); break;
        case StorageOp::Delete: finish(id, files_.erase(key) ? StorageStatus::Ok : StorageStatus::NotFound); break;
    }
}

void FileStorage::submit(int id, StorageOp op, const std::string& area, const std::string& name, std::string data) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::path(root_) / encodeStorageName(area);
    const fs::path file = dir / (encodeStorageName(name) + ".outlist");
    switch (op) {
        case StorageOp::Write: {
            fs::create_directories(dir, ec);
            if (ec) { finish(id, StorageStatus::Error); return; }
            const fs::path tmp = dir / (encodeStorageName(name) + ".outlist.tmp");
            {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                out.write(data.data(), static_cast<std::streamsize>(data.size()));
                out.flush();
                if (!out) { out.close(); fs::remove(tmp, ec); finish(id, StorageStatus::Error); return; }
            }
            fs::rename(tmp, file, ec);  // atomic replace on POSIX; replaces on Windows too
            if (ec) { fs::remove(tmp, ec); finish(id, StorageStatus::Error); return; }
            finish(id, StorageStatus::Ok);
            break;
        }
        case StorageOp::Read: {
            if (!fs::exists(file, ec)) { finish(id, StorageStatus::NotFound); return; }
            std::ifstream in(file, std::ios::binary);
            if (!in) { finish(id, StorageStatus::Error); return; }
            std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            finish(id, StorageStatus::Ok, std::move(contents));
            break;
        }
        case StorageOp::Exists: finish(id, fs::exists(file, ec) ? StorageStatus::Ok : StorageStatus::NotFound); break;
        case StorageOp::Delete: {
            const bool removed = fs::remove(file, ec);
            finish(id, ec ? StorageStatus::Error : (removed ? StorageStatus::Ok : StorageStatus::NotFound));
            break;
        }
    }
}

}  // namespace lithium
