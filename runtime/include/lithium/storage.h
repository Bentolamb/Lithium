// Persistent storage used by the OutList extension.
//
// Every operation is asynchronous: submit() returns immediately and the backend
// reports the result through the completion callback, possibly later (IndexedDB
// in a browser) or from inside submit() (memory, files). The runtime queues
// completions and delivers them at the start of the next step, so a block that
// is waiting on storage never resumes in the middle of another block.
#pragma once

#include <functional>
#include <vector>
#include <map>
#include <string>
#include <string_view>

namespace lithium {

enum class StorageOp { Write = 0, Read = 1, Exists = 2, Delete = 3 };
enum class StorageStatus { Ok = 0, NotFound = 1, Error = 2 };

class StorageBackend {
public:
    using Completion = std::function<void(int requestId, StorageStatus, std::string data)>;
    virtual ~StorageBackend() = default;
    void setCompletion(Completion c) { complete_ = std::move(c); }
    // `area` is the project scope, `name` a validated list name. For Write, `data` is the
    // file contents; Read completes with the contents; Exists and Delete carry none.
    // Write must be atomic or recoverable: after a crash the previous contents or the new
    // contents are readable, never a mixture.
    virtual void submit(int requestId, StorageOp op, const std::string& area, const std::string& name,
                        std::string data) = 0;
    // Synchronous listing of saved names for backends that can (memory, files). Returns
    // false when the platform has to supply the list asynchronously.
    virtual bool listNames(const std::string& area, std::vector<std::string>& out) {
        (void)area; (void)out;
        return false;
    }
    // Whether data written here survives closing the program.
    virtual bool persistent() const = 0;

protected:
    void finish(int id, StorageStatus s, std::string data = {}) {
        if (complete_) complete_(id, s, std::move(data));
    }

private:
    Completion complete_;
};

// Process-lifetime only. Default when the platform provides nothing.
class MemoryStorage : public StorageBackend {
public:
    void submit(int id, StorageOp op, const std::string& area, const std::string& name, std::string data) override;
    bool listNames(const std::string& area, std::vector<std::string>& out) override;
    bool persistent() const override { return false; }

private:
    std::map<std::pair<std::string, std::string>, std::string> files_;
};

// Files under a root directory: <root>/<area>/<encoded name>.outlist, written to a
// temporary file and renamed into place. Used by native builds and tests.
class FileStorage : public StorageBackend {
public:
    explicit FileStorage(std::string root) : root_(std::move(root)) {}
    void submit(int id, StorageOp op, const std::string& area, const std::string& name, std::string data) override;
    bool listNames(const std::string& area, std::vector<std::string>& out) override;
    bool persistent() const override { return true; }

private:
    std::string root_;
};

// File-system-safe form of a validated name: [a-z0-9_-] kept, every other byte (and every
// uppercase letter, so case-insensitive file systems cannot merge two names) as %XX.
std::string encodeStorageName(std::string_view name);
std::string decodeStorageName(std::string_view encoded);

}  // namespace lithium
