// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "curl_util.h"

#include <curl/curl.h>
#include <curl/urlapi.h>

#include "defer_util.h"
#include "../log.h"

void curl_util_init() {
	CURLcode cr = curl_global_init(CURL_GLOBAL_DEFAULT);
	if(cr != CURLE_OK) {
		log_error("Curl global init failed: {}", (int)cr);
	}
}
void curl_util_quit() {
	curl_global_cleanup();
}

std::string curl_util_build_url(std::string base, std::string path) {

	CURLU *url = curl_url();
	SCOPE_EXIT(curl_url_cleanup(url););

	CURLUcode uc;
	uc = curl_url_set(url, CURLUPART_URL, base.c_str(), 0);
	if(uc != CURLUE_OK) {
		log_error("curl_url_set base failed: {}", curl_url_strerror(uc));
		return {};
	}
	uc = curl_url_set(url, CURLUPART_PATH, path.c_str(), CURLU_URLENCODE);
	if(uc != CURLUE_OK) {
		log_error("curl_url_set path failed: {}", curl_url_strerror(uc));
		return {};
	}

	char *content = nullptr;
	uc = curl_url_get(url, CURLUPART_URL, &content, 0);
	if(uc != CURLUE_OK) {
		log_error("curl_url_get failed: {}", curl_url_strerror(uc));
		return {};
	}
	SCOPE_EXIT(curl_free(content););

	return std::string(content);
}
