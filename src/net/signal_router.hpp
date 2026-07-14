#pragma once

#include "net/signal_types.hpp"
#include "export.hpp"

namespace droidcli::net {

class SignalRouter {
public:
	void register_target(const SignalTarget& target);
	bool unregister_target(const core::String& target_id);
	core::Array<SignalTarget> list_targets() const;
	const SignalTarget* find_target(const core::String& target_id) const;

	SignalDispatchResult dispatch(
		const SignalEnvelope& envelope,
		const SignalTransportFn& transport) const;

	void append_log(const SignalLogEntry& entry);
	core::Array<SignalLogEntry> log_entries() const;
	void clear_log();

private:
	core::Array<SignalTarget> targets_;
	mutable core::Array<SignalLogEntry> log_;
	static constexpr size_t kMaxLogEntries = 128;
};

DROIDCLI_API core::String build_targets_json(const core::Array<SignalTarget>& targets);

DROIDCLI_API core::String build_signal_log_json(const core::Array<SignalLogEntry>& entries);

DROIDCLI_API bool parse_target_from_json(
	const core::String& json,
	SignalTarget& out_target,
	core::String& out_error);

} // namespace droidcli::net
