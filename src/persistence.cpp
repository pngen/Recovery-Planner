#include "recovery_planner/persistence.hpp"
#include "recovery_planner/digest.hpp"
#include "recovery_planner/error.hpp"

#include <fstream>
#include <filesystem>
#include <map>
#include <algorithm>

namespace recovery_planner {

namespace fs = std::filesystem;

namespace {

// Bounded maximum frame payload (resource discipline).
constexpr std::size_t kMaxPayload = 256u * 1024u * 1024u;

std::string sanitize_key(const std::string& k) {
  std::string out = k;
  for (char& ch : out) {
    char c = ch;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    if (!ok) ch = '_';
  }
  if (out.empty()) out = "state";
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// File-backed store
// ---------------------------------------------------------------------------
class FilePersistenceStore final : public PersistenceStore {
 public:
  explicit FilePersistenceStore(std::string dir) : dir_(std::move(dir)) {
    fs::create_directories(dir_);
  }

  void save(const std::string& key, const std::vector<std::uint8_t>& bytes) override {
    if (bytes.size() > kMaxPayload) {
      throw RecoveryError(ErrorCode::RESOURCE_EXHAUSTION, "persistence payload exceeds maximum size");
    }
    const std::string path = (fs::path(dir_) / (sanitize_key(key) + ".rpp")).string();

    ByteWriter w;
    w.u32(kPersistenceMagic);
    w.u32(kPersistenceFormatVersion);
    w.u32(static_cast<std::uint32_t>(bytes.size()));
    // payload
    for (std::uint8_t b : bytes) w.u8(b);
    w.u32(crc32c(bytes));

    const std::string tmp = path + ".tmp";
    {
      std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
      if (!ofs) throw RecoveryError(ErrorCode::PERSISTENCE_CORRUPTION, "cannot open temp file for write");
      ofs.write(reinterpret_cast<const char*>(w.data().data()),
                static_cast<std::streamsize>(w.size()));
      if (!ofs) throw RecoveryError(ErrorCode::PERSISTENCE_CORRUPTION, "failed to write temp persistence frame");
    }
    // Atomic replace: remove old, rename temp into place.
    std::error_code ec;
    fs::remove(path, ec);
    fs::rename(tmp, path, ec);
    if (ec) throw RecoveryError(ErrorCode::PERSISTENCE_CORRUPTION, "failed to commit persistence frame: " + ec.message());
  }

  std::optional<std::vector<std::uint8_t>> load(const std::string& key) override {
    const std::string path = (fs::path(dir_) / (sanitize_key(key) + ".rpp")).string();
    if (!fs::exists(path)) return std::nullopt;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return std::nullopt;
    std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    if (raw.size() < 16) return std::nullopt;  // too small to be a valid frame

    ByteReader r(raw);
    std::uint32_t magic = r.u32();
    std::uint32_t ver = r.u32();
    std::uint32_t len = r.u32();
    if (!r.ok() || magic != kPersistenceMagic) return std::nullopt;
    if (ver != kPersistenceFormatVersion) return std::nullopt;
    if (static_cast<std::size_t>(len) > kMaxPayload) return std::nullopt;
    if (!r.has(len + 4)) return std::nullopt;  // payload + crc
    std::vector<std::uint8_t> payload;
    if (!r.take(payload, len)) return std::nullopt;
    std::uint32_t stored_crc = r.u32();
    if (!r.ok()) return std::nullopt;
    // Reject trailing garbage.
    if (r.remaining() != 0) return std::nullopt;
    if (crc32c(payload) != stored_crc) return std::nullopt;
    return payload;
  }

  std::vector<std::string> keys() const override {
    std::vector<std::string> out;
    for (const auto& e : fs::directory_iterator(dir_)) {
      if (e.is_regular_file() && e.path().extension() == ".rpp") {
        out.push_back(e.path().stem().string());
      }
    }
    return out;
  }

  bool flush() override { return true; }
  const char* name() const noexcept override { return "file"; }

 private:
  std::string dir_;
};

// ---------------------------------------------------------------------------
// In-memory store
// ---------------------------------------------------------------------------
class MemoryPersistenceStore final : public PersistenceStore {
 public:
  void save(const std::string& key, const std::vector<std::uint8_t>& bytes) override {
    data_[key] = bytes;
  }
  std::optional<std::vector<std::uint8_t>> load(const std::string& key) override {
    auto it = data_.find(key);
    if (it == data_.end()) return std::nullopt;
    return it->second;
  }
  std::vector<std::string> keys() const override {
    std::vector<std::string> out;
    for (const auto& kv : data_) out.push_back(kv.first);
    return out;
  }
  bool flush() override { return true; }
  const char* name() const noexcept override { return "memory"; }

 private:
  std::map<std::string, std::vector<std::uint8_t>> data_;
};

std::shared_ptr<PersistenceStore> make_file_persistence_store(const std::string& directory) {
  return std::make_shared<FilePersistenceStore>(directory);
}

std::shared_ptr<PersistenceStore> make_memory_persistence_store() {
  return std::make_shared<MemoryPersistenceStore>();
}

}  // namespace recovery_planner
