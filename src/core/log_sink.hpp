#pragma once

#include <string_view>

namespace droidcli::core {

struct LogSink {
	virtual ~LogSink() = default;
	virtual void log_info(std::string_view message) = 0;
	virtual void log_warning(std::string_view message) = 0;
	virtual void log_verbose(std::string_view message) = 0;
};

void set_log_sink(LogSink* sink);
LogSink* get_log_sink();

} // namespace droidcli::core
