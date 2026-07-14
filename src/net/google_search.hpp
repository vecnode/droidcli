#pragma once

#include "core/types.hpp"
#include "export.hpp"

namespace droidcli::net {

struct GoogleSearchConfig {
	core::String api_key;
	core::String search_engine_id; // Google Programmable Search Engine ID ("cx")
	core::String query;
	int32_t result_count = 5; // Custom Search API caps at 10 per request
};

struct GoogleSearchResultItem {
	core::String title;
	core::String link;
	core::String snippet;
};

struct GoogleSearchResponse {
	bool success = false;
	core::String error_message;
	core::String total_results; // as reported by the API; may be an estimate
	core::Array<GoogleSearchResultItem> items;
};

/// Builds a Google Programmable Search Engine ("Custom Search JSON API")
/// request URL - a real Google Search API call over plain HTTP GET, not HTML
/// scraping of a search results page. Requires a free API key + search engine
/// ID from https://programmablesearchengine.google.com/ (free tier: 100
/// queries/day). Percent-encodes the query.
DROIDCLI_API core::String build_google_search_url(const GoogleSearchConfig& config);

/// Parses the JSON response body from the Custom Search API into a portable
/// result list. Hand-rolled (brace-depth) JSON array walk, consistent with the
/// rest of net/json.hpp - no JSON library dependency.
DROIDCLI_API GoogleSearchResponse parse_google_search_response(
	int32_t status_code,
	const core::String& response_body);

} // namespace droidcli::net
