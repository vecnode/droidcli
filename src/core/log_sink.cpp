#include "core/log_sink.hpp"

namespace droidcli::core {

namespace {
LogSink* g_log_sink = nullptr;
}

void set_log_sink(LogSink* sink)
{
	g_log_sink = sink;
}

LogSink* get_log_sink()
{
	return g_log_sink;
}

} // namespace droidcli::core
