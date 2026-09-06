#pragma once

// Versioned, integrity-checked persistence.
//
// A PersistenceStore persists opaque named blobs. The planner encodes its
// durable state into a versioned, CRC-protected canonical byte stream and
// commits it atomically. The store does not trust declared lengths and rejects
// truncation / corruption / trailing garbage.

#include "recovery_planner/types.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace recovery_planner {

// Durable persistence format identifiers.
constexpr std::uint32_t kPersistenceMagic = 0x5250u;  // "RP" header
constexpr std::uint32_t kPersistenceFormatVersion = 1u;

class PersistenceStore {
 public:
  virtual ~PersistenceStore() = default;

  // Persist a named blob (opaque to the store). Implementations must commit
  // only complete, integrity-checked frames.
  virtual void save(const std::string& key, const std::vector<std::uint8_t>& bytes) = 0;

  // Load a named blob. Returns std::nullopt when absent or corrupt/invalid.
  virtual std::optional<std::vector<std::uint8_t>> load(const std::string& key) = 0;

  virtual std::vector<std::string> keys() const = 0;
  virtual bool flush() = 0;
  virtual const char* name() const noexcept = 0;
};

// A file-backed store. Each key maps to a file "<dir>/<key>.rpp". Frames are
// written atomically (temp file + rename) and are CRC-32C protected with a
// format-version header.
std::shared_ptr<PersistenceStore> make_file_persistence_store(const std::string& directory);

// An in-memory store (for tests, and for the reference coordinator). Useful for
// inspecting round-trip semantics without the filesystem.
std::shared_ptr<PersistenceStore> make_memory_persistence_store();

}  // namespace recovery_planner
