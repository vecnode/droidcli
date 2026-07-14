#include "droidcli.h"

#include <cassert>
#include <iostream>

int main()
{
	using namespace droidcli::net;

	GoogleSearchConfig config;
	config.api_key = "AIzaTestKey";
	config.search_engine_id = "017576662512468239146:omuauf_lfve";
	config.query = "droidcli agent core";
	config.result_count = 5;

	const droidcli::core::String url = build_google_search_url(config);
	assert(url.find("https://www.googleapis.com/customsearch/v1?") == 0);
	assert(url.find("key=AIzaTestKey") != droidcli::core::String::npos);
	assert(url.find("cx=017576662512468239146:omuauf_lfve") != droidcli::core::String::npos
		|| url.find("cx=017576662512468239146%3Aomuauf_lfve") != droidcli::core::String::npos);
	assert(url.find("q=droidcli%20agent%20core") != droidcli::core::String::npos);
	assert(url.find("num=5") != droidcli::core::String::npos);

	const droidcli::core::String sample_response =
		"{"
		"\"searchInformation\":{\"totalResults\":\"12300\"},"
		"\"items\":["
		"{\"title\":\"Droidcli - agent core\",\"link\":\"https://example.com/one\","
		"\"snippet\":\"An overview of the droidcli control core.\"},"
		"{\"title\":\"Second result\",\"link\":\"https://example.com/two\","
		"\"snippet\":\"Another snippet with \\\"quotes\\\" inside.\"}"
		"]}";

	const GoogleSearchResponse ok_response = parse_google_search_response(200, sample_response);
	assert(ok_response.success);
	assert(ok_response.total_results == "12300");
	assert(ok_response.items.size() == 2);
	assert(ok_response.items[0].title == "Droidcli - agent core");
	assert(ok_response.items[0].link == "https://example.com/one");
	assert(ok_response.items[1].snippet.find("quotes") != droidcli::core::String::npos);

	const droidcli::core::String error_response =
		"{\"error\":{\"code\":403,\"message\":\"Daily Limit Exceeded\"}}";
	const GoogleSearchResponse failed_response = parse_google_search_response(403, error_response);
	assert(!failed_response.success);
	assert(failed_response.error_message == "Daily Limit Exceeded");
	assert(failed_response.items.empty());

	std::cout << "google_search_test passed" << std::endl;
	return 0;
}
