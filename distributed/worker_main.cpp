#include "transport.hpp"
#include "protocol.hpp"
#include "recovery_planner/digest.hpp"
#include "recovery_planner/strategy.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace recovery_planner;
using namespace recovery_planner::transport;
using namespace recovery_planner::proto;

namespace {
// Deterministic synthetic work: produce a checkpoint byte blob for a progress
// boundary. The blob is derived deterministically from worker seed + progress.
std::vector<std::uint8_t> checkpoint_for(std::uint64_t worker_id, std::uint64_t progress,
                                         const std::string& role) {
  std::string seed = role + ":" + std::to_string(worker_id) + ":" + std::to_string(progress);
  auto h = sha256(reinterpret_cast<const std::uint8_t*>(seed.data()), seed.size());
  std::vector<std::uint8_t> blob(h.begin(), h.end());
  return blob;
}

bool send(Connection& c, const Msg& m) {
  std::vector<std::uint8_t> payload;
  encode(m, payload);
  return c.send_frame(payload);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 6) {
    std::fprintf(stderr, "worker usage: worker <port> <worker_id> <boot_id> <engine> <role>\n");
    return 2;
  }
  std::uint16_t port = static_cast<std::uint16_t>(std::atoi(argv[1]));
  std::uint64_t worker_id = std::strtoull(argv[2], nullptr, 10);
  std::uint64_t boot_id = std::strtoull(argv[3], nullptr, 10);
  std::uint64_t engine = std::strtoull(argv[4], nullptr, 10);
  std::string role = argv[5];

  auto conn = connect_to({"127.0.0.1", port});
  if (!conn) { std::fprintf(stderr, "worker: connect failed\n"); return 3; }

  Msg reg; reg.type = MsgType::REGISTER;
  reg.a = worker_id; reg.b = boot_id; reg.c = engine; reg.s1 = role;
  send(*conn, reg);

  while (true) {
    std::vector<std::uint8_t> payload;
    if (!conn->recv_frame(payload)) break;
    Msg m;
    if (!decode(payload, m)) continue;  // ignore malformed (defensive)

    switch (m.type) {
      case MsgType::STOP:
        return 0;
      case MsgType::RUN: {
        std::uint64_t target = m.b;
        // Synthetic work simulation: advance through the PRNG to "reach" target.
        std::uint64_t w = 0x9E3779B97F4A7C15ull * (worker_id + 1);
        for (std::uint64_t i = 0; i < target; ++i) w = (w ^ (w >> 30)) * 0xBF58476D1CE4E5B9ull + 1;
        (void)w;
        Msg done; done.type = MsgType::WORK_DONE;
        done.a = worker_id;
        done.b = target;                 // progress
        done.c = m.a;                    // work id
        done.blob = checkpoint_for(worker_id, target, role);
        done.s1 = role;                  // provenance label
        send(*conn, done);
        break;
      }
      case MsgType::DISPATCH: {
        std::uint64_t plan_id = m.a;
        std::uint64_t generation = m.b;
        std::uint64_t strategy = m.c;
        std::uint64_t expected_recovered = m.d;
        // The worker performs the (synthetic) mechanism and reports recovered
        // progress. RECOMPUTE may report a different boundary; we model it as an
        // independent recomputation that yields the same expected progress when
        // recomputation is permitted.
        std::uint64_t recovered = expected_recovered;
        Msg comp; comp.type = MsgType::COMPLETE;
        comp.a = plan_id;
        comp.b = generation;
        comp.c = boot_id;                 // authoritative reporter identity
        comp.d = 0;                       // SUCCESS
        comp.s1 = std::string(to_string(static_cast<RecoveryStrategy>(strategy))) + " performed by worker " +
                  std::to_string(worker_id);
        comp.s2 = std::to_string(recovered);
        send(*conn, comp);
        break;
      }
      default:
        break;
    }
  }
  return 0;
}
