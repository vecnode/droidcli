#pragma once

#include "core/types.hpp"
#include "export.hpp"

#include <functional>
#include <thread>

namespace droidcli::core {

// Reports a background thread's lifecycle: "spawned" immediately, then
// "joined" or "threw: <what>" when the thread's function returns or throws.
// core::spawn has no logging mechanism of its own (portable, no I/O) - a
// caller (typically a cli/ host) wires this to whatever durable log it
// already has, e.g. DroidHost::log_thread_event appending under the
// "thread" channel of logs/log.jsonl (see ARCHITECTURE.md's "Structured
// JSONL logging" and "Spawn attribution" sections).
using ThreadEventSink = std::function<void(const core::String& thread_name, const core::String& event)>;

// droidcli's Core-tier "spawn" role (see ARCHITECTURE.md's crate comparison
// to ZeroClaw's zeroclaw-spawn): a sanctioned, named std::thread
// construction. Not a thread pool, not a scheduler - one thread in, one
// thread out, same as a bare std::thread construction, just named and
// optionally observable via `sink`. Exceptions still propagate out of the
// thread function exactly as they would from a bare std::thread (an
// uncaught exception still calls std::terminate()) - `sink` gets a last
// chance to report why before that happens, it does not swallow the
// exception or change std::thread's behavior.
DROIDCLI_API std::thread spawn(const core::String& thread_name, std::function<void()> fn, ThreadEventSink sink = {});

} // namespace droidcli::core
