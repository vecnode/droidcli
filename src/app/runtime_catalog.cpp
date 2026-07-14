#include "app/runtime_catalog.hpp"

#include "net/json.hpp"

#include <sstream>

namespace droidcli::app {
namespace {

RuntimeDescriptor make_runtime(
	const char* id,
	const char* title,
	const char* summary,
	const char* host_scope,
	const bool active)
{
	RuntimeDescriptor descriptor;
	descriptor.id = id;
	descriptor.title = title;
	descriptor.summary = summary;
	descriptor.host_scope = host_scope;
	descriptor.active_in_session = active;
	return descriptor;
}

} // namespace

core::Array<RuntimeDescriptor> build_runtime_catalog(
	const session::RuntimeSession& session)
{
	core::Array<RuntimeDescriptor> catalog;
	catalog.push_back(make_runtime(
		"networking",
		"Networking Engine",
		"Signal bus, target registry, HTTP inbound/outbound.",
		"core",
		session.features.networking && session.http_enabled));
	catalog.push_back(make_runtime(
		"media",
		"Media Runtime",
		"FFmpeg media probe/decode and corpus (OCR/objects) reading.",
		"core",
		true));
	catalog.push_back(make_runtime(
		"ai",
		"Language AI Runtime",
		"Ollama chat integration, transcript, optional autopilot planning.",
		"core",
		session.features.ai));
	catalog.push_back(make_runtime(
		"session",
		"Session + Commands",
		"RuntimeSession snapshot and command parse/validate.",
		"core",
		session.active));
	catalog.push_back(make_runtime(
		"notify",
		"Notify Ingest",
		"Inbound /notify events, parse JSON/text, append to comms log.",
		"core",
		session.http_enabled));
	catalog.push_back(make_runtime(
		"recording",
		"Recording Runtime",
		"Capture toggles and status queries via host services.",
		"core",
		session.features.recording));
	catalog.push_back(make_runtime(
		"autopilot",
		"Autopilot Runtime",
		"AI-driven control loop toggles and status.",
		"core",
		session.features.ai));
	return catalog;
}

core::String build_runtime_catalog_json(
	const session::RuntimeSession& session)
{
	const core::Array<RuntimeDescriptor> catalog = build_runtime_catalog(session);
	std::ostringstream stream;
	stream << '{';
	stream << "\"runtimes\":[";
	for (size_t index = 0; index < catalog.size(); ++index)
	{
		if (index > 0)
		{
			stream << ',';
		}
		const RuntimeDescriptor& runtime = catalog[index];
		stream << '{';
		stream << net::json_string_field("id", runtime.id) << ',';
		stream << net::json_string_field("title", runtime.title) << ',';
		stream << net::json_string_field("summary", runtime.summary) << ',';
		stream << net::json_string_field("host_scope", runtime.host_scope) << ',';
		stream << net::json_bool_field("active_in_session", runtime.active_in_session);
		stream << '}';
	}
	stream << "]}";
	return stream.str();
}

} // namespace droidcli::app
