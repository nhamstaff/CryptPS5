#ifndef DECRYPT_PFS_HPP
#define DECRYPT_PFS_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <filesystem>
#include <algorithm>

namespace Decrypt {

// ──────────────────────────────────────────────────────────────────────
//  PFS (PlayStation File System) — inner filesystem for PS5 partitions
//
//  Found inside:
//    - system / system_ex partition images (from firmware PUPs)
//    - Game PKG payloads (pfs_image.dat)
//    - Application FPKGs
//
//  Layout:
//    Block-based filesystem with 64 KiB blocks (0x10000 bytes)
//    Superblock at block 0 → describes root directory inode
//    Directory entries are variable-length records
//    Files are stored as contiguous or block-mapped extents
// ──────────────────────────────────────────────────────────────────────

static constexpr uint64_t PFS_MAGIC     = 0x20203346535000ULL;  // "PFS  " padded
static constexpr uint32_t PFS_BLOCK_SIZE = 0x10000;             // 64 KiB mandatory

#pragma pack(push, 1)

struct PFSSuperblock {
    uint64_t magic;
    uint64_t version;
    uint64_t mode;
    uint64_t block_size;
    uint64_t block_count;
    uint64_t inode_count;
    uint64_t root_dir_inode;
    uint64_t root_dir_block;
    uint8_t  reserved[64];  // padding to 128 bytes
};

// PFS inode — describes a file or directory
struct PFSInode {
    uint32_t mode;           // file type + permissions
    uint32_t nlinks;
    uint32_t flags;
    uint64_t size;
    uint64_t size_compressed;
    uint64_t time_created;
    uint64_t time_modified;
    uint64_t time_accessed;
    uint32_t uid;
    uint32_t gid;
    uint64_t blocks[12];    // direct block pointers
    uint64_t indirect;       // single indirect block
    uint64_t double_indirect;
    uint8_t  padding[8];
};

// PFS directory entry — variable-length
struct PFSDirEntry {
    uint32_t inode_index;
    uint32_t entry_type;     // 0x02 = file, 0x04 = directory
    uint32_t name_length;
    uint32_t entry_size;     // total size including name + padding
    // followed by name_length bytes of filename
};

#pragma pack(pop)

// ── Inode type flags ──

static constexpr uint32_t PFS_INODE_FILE = 0x8000;
static constexpr uint32_t PFS_INODE_DIR  = 0x4000;
static constexpr uint32_t PFS_ENTRY_FILE = 0x02;
static constexpr uint32_t PFS_ENTRY_DIR  = 0x04;

// ── Parsed types ──

struct PFSFile {
    std::string path;
    uint64_t    size;
    uint64_t    offset;          // absolute byte offset in image
    uint32_t    inode_index;
    bool        is_directory;
};

struct PFSImage {
    PFSSuperblock             superblock;
    std::vector<PFSFile>      files;
    std::vector<uint8_t>      raw_data;
    uint64_t                  block_size;

    bool valid() const {
        return superblock.block_size >= 512 &&
               superblock.block_count > 0;
    }
};

// ── Block reader utility ──

inline const uint8_t* read_block(const PFSImage& image, uint64_t block_idx) {
    uint64_t offset = block_idx * image.block_size;
    if (offset + image.block_size > image.raw_data.size())
        return nullptr;
    return image.raw_data.data() + offset;
}

// ── Parse directory tree recursively ──

inline void parse_directory(
    PFSImage& image,
    uint64_t dir_block,
    uint64_t dir_size,
    const std::string& current_path)
{
    uint64_t offset = dir_block * image.block_size;
    uint64_t end    = offset + dir_size;

    if (end > image.raw_data.size()) end = image.raw_data.size();

    uint64_t pos = offset;
    while (pos + sizeof(PFSDirEntry) <= end) {
        PFSDirEntry entry{};
        std::memcpy(&entry, image.raw_data.data() + pos, sizeof(PFSDirEntry));

        if (entry.entry_size == 0) break;
        if (entry.name_length == 0 || entry.inode_index == 0) {
            pos += entry.entry_size;
            continue;
        }

        // read filename
        uint64_t name_offset = pos + sizeof(PFSDirEntry);
        if (name_offset + entry.name_length > end) break;

        std::string name(reinterpret_cast<const char*>(
            image.raw_data.data() + name_offset), entry.name_length);

        // skip . and ..
        if (name == "." || name == "..") {
            pos += entry.entry_size;
            continue;
        }

        std::string full_path = current_path.empty()
                                ? name : current_path + "/" + name;

        PFSFile file;
        file.path         = full_path;
        file.inode_index  = entry.inode_index;
        file.is_directory = (entry.entry_type == PFS_ENTRY_DIR);

        // read inode for this entry to get size + block pointers
        // inode table starts after superblock (block 1+)
        uint64_t inode_offset = image.block_size +
                                entry.inode_index * sizeof(PFSInode);
        if (inode_offset + sizeof(PFSInode) <= image.raw_data.size()) {
            PFSInode inode{};
            std::memcpy(&inode, image.raw_data.data() + inode_offset,
                        sizeof(PFSInode));
            file.size   = inode.size;
            file.offset = inode.blocks[0] * image.block_size;

            image.files.push_back(file);

            if (file.is_directory && inode.blocks[0] != 0) {
                parse_directory(image, inode.blocks[0], inode.size, full_path);
            }
        }

        pos += entry.entry_size;
    }
}

// ── Parse PFS image ──

inline PFSImage parse_pfs(const std::vector<uint8_t>& data) {
    PFSImage image{};
    image.raw_data = data;

    if (data.size() < sizeof(PFSSuperblock))
        throw std::runtime_error("PFS: image too small for superblock");

    std::memcpy(&image.superblock, data.data(), sizeof(PFSSuperblock));

    image.block_size = image.superblock.block_size;
    if (image.block_size == 0) image.block_size = PFS_BLOCK_SIZE;

    // validate block size is power of 2 and reasonable
    if (image.block_size < 512 || (image.block_size & (image.block_size - 1)) != 0)
        throw std::runtime_error("PFS: invalid block size " +
                                  std::to_string(image.block_size));

    if (image.superblock.root_dir_block != 0) {
        // read root inode
        uint64_t root_inode_off = image.block_size +
                                  image.superblock.root_dir_inode * sizeof(PFSInode);
        if (root_inode_off + sizeof(PFSInode) <= data.size()) {
            PFSInode root_inode{};
            std::memcpy(&root_inode, data.data() + root_inode_off, sizeof(PFSInode));
            parse_directory(image, image.superblock.root_dir_block,
                           root_inode.size, "");
        }
    }

    return image;
}

// ── Extract a file from PFS ──

inline std::vector<uint8_t> extract_pfs_file(
    const PFSImage& image, const PFSFile& file)
{
    if (file.is_directory)
        throw std::runtime_error("PFS: cannot extract directory as file");

    if (file.offset + file.size > image.raw_data.size())
        throw std::runtime_error("PFS: file '" + file.path +
                                  "' extends past image boundary");

    return {image.raw_data.begin() + file.offset,
            image.raw_data.begin() + file.offset + file.size};
}

// ── Find files matching a pattern ──

inline std::vector<const PFSFile*> find_files(
    const PFSImage& image,
    const std::string& suffix)
{
    std::vector<const PFSFile*> results;
    for (const auto& file : image.files) {
        if (!file.is_directory && file.path.size() >= suffix.size() &&
            file.path.compare(file.path.size() - suffix.size(),
                              suffix.size(), suffix) == 0)
            results.push_back(&file);
    }
    return results;
}

// ── Find all PRX/SPRX modules ──

inline std::vector<const PFSFile*> find_prx_modules(const PFSImage& image) {
    std::vector<const PFSFile*> results;
    for (const auto& file : image.files) {
        if (file.is_directory) continue;
        if (file.path.ends_with(".prx") || file.path.ends_with(".sprx") ||
            file.path.ends_with(".suprx"))
            results.push_back(&file);
    }
    return results;
}

// ── Dump entire PFS to disk ──

inline void dump_pfs(const PFSImage& image,
                     const std::filesystem::path& output_dir) {
    std::filesystem::create_directories(output_dir);

    for (const auto& file : image.files) {
        auto path = output_dir / file.path;
        if (file.is_directory) {
            std::filesystem::create_directories(path);
        } else {
            std::filesystem::create_directories(path.parent_path());
            auto blob = extract_pfs_file(image, file);
            FILE* fp = fopen(path.string().c_str(), "wb");
            if (!fp) throw std::runtime_error("PFS: cannot write " +
                                               path.string());
            fwrite(blob.data(), 1, blob.size(), fp);
            fclose(fp);
        }
    }
}

}  // namespace Decrypt

#endif  // DECRYPT_PFS_HPP
