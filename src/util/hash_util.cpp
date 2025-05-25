// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Eli2
#include "hash_util.h"

#include <iomanip>
#include <sstream>

#include "hash/md5.h"
#include "hash/sha1.h"

std::string calc_md5(const std::span<const std::byte> data) {
	MD5_CTX ctx;
	MD5_Init(&ctx);
	MD5_Update(&ctx, data.data(), data.size());
	
	uint32_t digest[4];
	MD5_Final(digest, &ctx);
	
	std::ostringstream buf;
	for(int i = 0; i < 4; ++i)
		buf << std::hex << std::setfill('0') << std::setw(8) << digest[i];
	
	return buf.str();
}

std::string calc_sha1(const std::span<const std::byte> data) {
	SHA1_CTX ctx;
	SHA1Init(&ctx);
	SHA1Update(&ctx, (const unsigned char *)data.data(), data.size());
	
	uint32_t digest[5];
	SHA1Final(digest, &ctx);
	
	std::ostringstream buf;
	for(int i = 0; i < 5; ++i)
		buf << std::hex << std::setfill('0') << std::setw(8) << digest[i];
	
	return buf.str();
}


