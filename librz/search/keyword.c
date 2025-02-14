// SPDX-FileCopyrightText: 2010-2015 pancake <pancake@nopcode.org>
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_search.h>

static int ignoreMask(const ut8 *bm, int len) {
	int i;
	for (i = 0; i < len; i++) {
		if (bm[i] != 0xff) {
			return 0;
		}
	}
	return 1;
}

/**
 * \brief Initializes a RzSearchKeyword with keyword bytes, optional bitmask and optional data.
 *
 * \param kw_buf Byte buffer of the keyword.
 * \param kw_buf_len Length of \p kw_buf in bytes.
 * \param bm_buf A bitmask. It is applied the bytes before comparing it to the keyword bytes.
 * \param bm_buf_len Length of the bitmask buffer \p bm_buf in bytes.
 * \param data Pointer to the data to search in.
 *
 * \return The initialized RzSearchKeyword or NULL in case of failure.
 */
RZ_API RZ_OWN RzSearchKeyword *rz_search_keyword_new(const ut8 *kw_buf, int kw_len, RZ_NULLABLE const ut8 *bm_buf, int bm_buf_len, RZ_NULLABLE const char *data) {
	RzSearchKeyword *kw;
	if (kw_len < 1 || bm_buf_len < 0) {
		return NULL;
	}
	kw = RZ_NEW0(RzSearchKeyword);
	if (!kw) {
		return NULL;
	}
	kw->type = RZ_SEARCH_KEYWORD_TYPE_BINARY;
	kw->data = (void *)data;
	kw->keyword_length = kw_len;
	kw->bin_keyword = malloc(kw_len);
	if (!kw->bin_keyword) {
		rz_search_keyword_free(kw);
		return NULL;
	}
	memcpy(kw->bin_keyword, kw_buf, kw_len);
	if (bm_buf && bm_buf_len > 0 && !ignoreMask(bm_buf, bm_buf_len)) {
		kw->bin_binmask = malloc(bm_buf_len);
		if (!kw->bin_binmask) {
			rz_search_keyword_free(kw);
			return NULL;
		}
		memcpy(kw->bin_binmask, bm_buf, bm_buf_len);
		kw->binmask_length = bm_buf_len;
	} else {
		kw->bin_binmask = NULL;
		kw->binmask_length = 0;
	}
	return kw;
}

RZ_API void rz_search_keyword_free(RzSearchKeyword *kw) {
	if (!kw) {
		return;
	}
	free(kw->bin_binmask);
	free(kw->bin_keyword);
	free(kw);
}

RZ_API RzSearchKeyword *rz_search_keyword_new_str(const char *kwbuf, const char *bmstr, const char *data, int ignore_case) {
	RzSearchKeyword *kw;
	ut8 *bmbuf = NULL;
	int bmlen = 0;

	if (bmstr) {
		bmbuf = malloc(strlen(bmstr) + 1);
		if (!bmbuf) {
			return NULL;
		}
		bmlen = rz_hex_str2bin(bmstr, bmbuf);
		if (bmlen < 1) {
			RZ_FREE(bmbuf);
		}
	}
	kw = rz_search_keyword_new((ut8 *)kwbuf, strlen(kwbuf), bmbuf, bmlen, data);
	if (kw) {
		kw->icase = ignore_case;
		kw->type = RZ_SEARCH_KEYWORD_TYPE_STRING;
	}
	free(bmbuf);
	return kw;
}

RZ_API RzSearchKeyword *rz_search_keyword_new_wide(const char *kwbuf, const char *bmstr, const char *data, int ignore_case) {
	RzSearchKeyword *kw;
	int len;
	const char *p2;
	char *p, *str;
	ut8 *bmbuf = NULL;
	int bmlen = 0;

	if (bmstr) {
		bmbuf = malloc(strlen(bmstr) + 1);
		if (!bmbuf) {
			return NULL;
		}
		bmlen = rz_hex_str2bin(bmstr, bmbuf);
		if (bmlen < 1) {
			RZ_FREE(bmbuf);
		}
	}

	len = strlen(kwbuf);
	str = malloc((len + 1) * 2);
	for (p2 = kwbuf, p = str; *p2;) {
		RzCodePoint ch;
		int num_utf8_bytes = rz_utf8_decode((const ut8 *)p2, kwbuf + len - p2, &ch);
		if (num_utf8_bytes < 1) {
			eprintf("WARNING: Malformed UTF8 at pos %td\n", p2 - kwbuf);
			p[0] = *p2;
			p[1] = 0;
			p2++;
			p += 2;
			continue;
		}
		if (ignore_case && ch <= 0xff) {
			ch = tolower(ch);
		}
		int num_wide_bytes = rz_utf16le_encode((ut8 *)p, ch);
		rz_warn_if_fail(num_wide_bytes != 0);
		p2 += num_utf8_bytes;
		p += num_wide_bytes;
	}

	kw = rz_search_keyword_new((ut8 *)str, p - str, bmbuf, bmlen, data);
	free(str);
	if (kw) {
		kw->icase = ignore_case;
	}
	free(bmbuf);
	return kw;
}

RZ_API RzSearchKeyword *rz_search_keyword_new_hex(const char *kwstr, const char *bmstr, const char *data) {
	RzSearchKeyword *kw;
	ut8 *kwbuf, *bmbuf;
	int kwlen, bmlen = 0;

	if (!kwstr) {
		return NULL;
	}

	kwbuf = malloc(strlen(kwstr) + 1);
	if (!kwbuf) {
		return NULL;
	}

	kwlen = rz_hex_str2bin(kwstr, kwbuf);
	if (kwlen < 1) {
		free(kwbuf);
		return NULL;
	}

	bmbuf = NULL;
	if (bmstr && *bmstr) {
		bmbuf = malloc(strlen(bmstr) + 1);
		if (!bmbuf) {
			free(kwbuf);
			return NULL;
		}
		bmlen = rz_hex_str2bin(bmstr, bmbuf);
		if (bmlen < 1) {
			free(bmbuf);
			free(kwbuf);
			return NULL;
		}
	}

	kw = rz_search_keyword_new(kwbuf, kwlen, bmbuf, bmlen, data);
	free(kwbuf);
	free(bmbuf);
	return kw;
}

RZ_API RzSearchKeyword *rz_search_keyword_new_hexmask(const char *kwstr, const char *data) {
	RzSearchKeyword *ks = NULL;
	ut8 *kw, *bm;
	if (kwstr != NULL) {
		int len = strlen(kwstr);
		kw = malloc(len + 4);
		bm = malloc(len + 4);
		if (kw != NULL && bm != NULL) {
			len = rz_hex_str2binmask(kwstr, (ut8 *)kw, (ut8 *)bm);
			if (len < 0) {
				len = -len - 1;
			}
			if (len > 0) {
				ks = rz_search_keyword_new(kw, len, bm, len, data);
			}
		}
		free(kw);
		free(bm);
	}
	return ks;
}

/* Validate a regexp in the canonical format /<regexp>/<options> */
RZ_API RzSearchKeyword *rz_search_keyword_new_regexp(const char *str, const char *data) {
	RzSearchKeyword *kw;
	int i = 0, start, length;

	while (isspace((const unsigned char)str[i])) {
		i++;
	}

	if (str[i++] != '/') {
		return NULL;
	}

	/* Find the fist non backslash-escaped slash */
	int specials = 0;
	for (start = i; str[i]; i++) {
		if (str[i] == '/' && str[i - 1] != '\\') {
			break;
		} else if (str[i - 1] == '\\' && isalpha(str[i])) {
			specials++;
		}
	}

	if (str[i++] != '/') {
		return NULL;
	}

	length = i - start - 1;
	if ((length > 128) || (length < 1)) {
		return NULL;
	}

	kw = RZ_NEW0(RzSearchKeyword);
	if (!kw) {
		return NULL;
	}

	kw->bin_keyword = malloc(length + 1);
	if (!kw->bin_keyword) {
		rz_search_keyword_free(kw);
		return NULL;
	}

	kw->bin_keyword[length] = 0;
	memcpy(kw->bin_keyword, str + start, length);
	kw->keyword_length = length - specials;
	kw->type = RZ_SEARCH_KEYWORD_TYPE_STRING;
	kw->data = (void *)data;

	/* Parse the options */
	for (; str[i]; i++) {
		switch (str[i]) {
		case 'i':
			kw->icase = true;
			break;
		default:
			rz_search_keyword_free(kw);
			return NULL;
		}
	}

	return kw;
}
