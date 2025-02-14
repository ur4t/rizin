// SPDX-FileCopyrightText: 2012-2021 pancake <pancake@nopcode.org>
// SPDX-License-Identifier: LGPL-3.0-only

#include <rz_socket.h>
#include <rz_util.h>

static bool *breaked = NULL;

RZ_API void rz_socket_http_server_set_breaked(bool *b) {
	breaked = b;
}

RZ_API RzSocketHTTPRequest *rz_socket_http_accept(RzSocket *s, RzSocketHTTPOptions *so) {
	int content_length = 0, xx, yy;
	int pxx = 1, first = 0;
	char buf[1500], *p, *q;
	RzSocketHTTPRequest *hr = RZ_NEW0(RzSocketHTTPRequest);
	if (!hr) {
		return NULL;
	}
	if (so->accept_timeout) {
		hr->s = rz_socket_accept_timeout(s, 1);
	} else {
		hr->s = rz_socket_accept(s);
	}
	if (!hr->s) {
		free(hr);
		return NULL;
	}
	if (so->timeout > 0) {
		rz_socket_block_time(hr->s, true, so->timeout, 0);
	}
	hr->auth = !so->httpauth;
	for (;;) {
#if __WINDOWS__
		if (breaked && *breaked) {
			rz_socket_http_close(hr);
			return NULL;
		}
#endif
		memset(buf, 0, sizeof(buf));
		xx = rz_socket_gets(hr->s, buf, sizeof(buf));
		yy = rz_socket_ready(hr->s, 0, 20 * 1000); // this function uses usecs as argument
		//		eprintf ("READ %d (%s) READY %d\n", xx, buf, yy);
		if (!yy || (!xx && !pxx)) {
			break;
		}
		pxx = xx;

		if (first == 0) {
			first = 1;
			if (strlen(buf) < 3) {
				rz_socket_http_close(hr);
				return NULL;
			}
			p = strchr(buf, ' ');
			if (p) {
				*p = 0;
			}
			hr->method = rz_str_dup(buf);
			if (p) {
				q = strstr(p + 1, " HTTP"); // strchr (p+1, ' ');
				if (q) {
					*q = 0;
				}
				hr->path = rz_str_dup(p + 1);
			}
		} else {
			if (!hr->referer && !strncmp(buf, "Referer: ", 9)) {
				hr->referer = rz_str_dup(buf + 9);
			} else if (!hr->agent && !strncmp(buf, "User-Agent: ", 12)) {
				hr->agent = rz_str_dup(buf + 12);
			} else if (!hr->host && !strncmp(buf, "Host: ", 6)) {
				hr->host = rz_str_dup(buf + 6);
			} else if (!strncmp(buf, "Content-Length: ", 16)) {
				content_length = atoi(buf + 16);
			} else if (so->httpauth && !strncmp(buf, "Authorization: Basic ", 21)) {
				char *authtoken = buf + 21;
				size_t authlen = strlen(authtoken);
				char *curauthtoken;
				RzListIter *iter;
				char *decauthtoken = calloc(4, authlen + 1);
				if (!decauthtoken) {
					eprintf("Could not allocate decoding buffer\n");
					return hr;
				}

				if (rz_base64_decode((ut8 *)decauthtoken, authtoken, authlen) == -1) {
					eprintf("Could not decode authorization token\n");
				} else {
					rz_list_foreach (so->authtokens, iter, curauthtoken) {
						if (!strcmp(decauthtoken, curauthtoken)) {
							hr->auth = true;
							break;
						}
					}
				}

				free(decauthtoken);

				if (!hr->auth) {
					eprintf("Failed attempt login from '%s'\n", hr->host);
				}
			}
		}
	}
	if (content_length > 0) {
		rz_socket_read_block(hr->s, (ut8 *)buf, 1); // one missing byte
		if (ST32_ADD_OVFCHK(content_length, 1)) {
			rz_socket_http_close(hr);
			eprintf("Could not allocate hr data\n");
			return NULL;
		}
		hr->data = malloc(content_length + 1);
		hr->data_length = content_length;
		rz_socket_read_block(hr->s, hr->data, hr->data_length);
		hr->data[content_length] = 0;
	}
	return hr;
}

RZ_API void rz_socket_http_response(RzSocketHTTPRequest *rs, int code, const char *out, int len, const char *headers) {
	const char *strcode =
		code == 200 ? "ok" : code == 301 ? "Moved permanently"
		: code == 302                    ? "Found"
		: code == 401                    ? "Unauthorized"
		: code == 403                    ? "Permission denied"
		: code == 404                    ? "not found"
						 : "UNKNOWN";
	if (len < 1) {
		len = out ? strlen(out) : 0;
	}
	if (!headers) {
		headers = code == 401 ? "WWW-Authenticate: Basic realm=\"R2 Web UI Access\"\n" : "";
	}
	rz_socket_printf(rs->s, "HTTP/1.0 %d %s\r\n%s"
				"Connection: close\r\nContent-Length: %d\r\n\r\n",
		code, strcode, headers, len);
	if (out && len > 0) {
		rz_socket_write(rs->s, (void *)out, len);
	}
}

RZ_API ut8 *rz_socket_http_handle_upload(const ut8 *str, int len, int *retlen) {
	if (retlen) {
		*retlen = 0;
	}
	if (!strncmp((const char *)str, "----------", 10)) {
		int datalen;
		char *ret;
		const char *data, *token = (const char *)str + 10;
		const char *end = strchr(token, '\n');
		if (!end) {
			return NULL;
		}
		data = strstr(end, "Content-Disposition: form-data; ");
		if (data) {
			data = strchr(data, '\n');
			if (data) {
				data = strchr(data + 1, '\n');
			}
		}
		if (data) {
			while (*data == 10 || *data == 13) {
				data++;
			}
			end = (const char *)str + len - 40;
			while (*end == '-') {
				end--;
			}
			if (*end == 10 || *end == 13) {
				end--;
			}
			datalen = (size_t)(end - data);
			ret = malloc(datalen + 1);
			if (!ret) {
				return NULL;
			}
			memcpy(ret, data, datalen);
			ret[datalen] = 0;
			if (retlen) {
				*retlen = datalen;
			}
			return (ut8 *)ret;
		}
	}
	return NULL;
}

/* close client socket and free struct */
RZ_API void rz_socket_http_close(RzSocketHTTPRequest *rs) {
	rz_socket_free(rs->s);
	free(rs->path);
	free(rs->host);
	free(rs->agent);
	free(rs->method);
	free(rs->data);
	free(rs);
}

#if MAIN
int main() {
	RzSocket *s = rz_socket_new(false);
	if (!rz_socket_listen(s, "8080", NULL)) {
		eprintf("Cannot listen here\n");
		return 1;
	}
	for (;;) {
		RzSocketHTTPRequest *rs = rz_socket_http_accept(s, 0);
		if (!rs)
			continue;
		if (!strcmp(rs->method, "GET")) {
			rz_socket_http_response(rs, 200,
				"<html><body><form method=post action=/>"
				"<input name=a /><input type=button></form></body>");
		} else if (!strcmp(rs->method, "POST")) {
			char *buf = malloc(rs->data_length + 50);
			strcpy(buf, "<html><body><h2>XSS test</h2>\n");
			rz_str_unescape(rs->data);
			strcat(buf, rs->data);
			rz_socket_http_response(rs, 200, buf);
			free(buf);
		} else if (!strcmp(rs->method, "OPTIONS")) {
			rz_socket_http_response(rs, 200, "");
		} else {
			rz_socket_http_response(rs, 404, "Invalid protocol");
		}
		rz_socket_http_close(rs);
	}
}
#endif
