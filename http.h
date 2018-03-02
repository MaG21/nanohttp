/*-
 * Copyright 2012 Matthew Endsley
 * All rights reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted providing that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef HTTP_H
#define HTTP_H

#include <ctype.h>
#include <string.h>

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Parses the size out of a chunk-encoded HTTP response. Returns non-zero if it
 * needs more data. Retuns zero success or error. When error: size == -1 On
 * success, size = size of following chunk data excluding trailing \r\n. User is
 * expected to process or otherwise seek past chunk data up to the trailing
 * \r\n. The state parameter is used for internal state and should be
 * initialized to zero the first call.
 */
int http_parse_chunked(int* state, int *size, char ch);

enum http_header_status
{
    http_header_status_done,
    http_header_status_continue,
    http_header_status_version_character,
    http_header_status_code_character,
    http_header_status_status_character,
    http_header_status_key_character,
    http_header_status_value_character,
    http_header_status_store_keyvalue
};

/**
 * Parses a single character of an HTTP header stream. The state parameter is
 * used as internal state and should be initialized to zero for the first call.
 * Return value is a value from the http_header_status enuemeration specifying
 * the semantics of the character. If an error is encountered,
 * http_header_status_done will be returned with a non-zero state parameter. On
 * success http_header_status_done is returned with the state parameter set to
 * zero.
 */
int http_parse_header_char(int* state, char ch);


/**
 * Callbacks for handling response data.
 *  realloc_scratch - reallocate memory, cannot fail. There will only
 *                    be one scratch buffer. Implemnentation may take
 *                    advantage of this fact.
 *  body - handle HTTP response body data
 *  header - handle an HTTP header key/value pair
 *  code - handle the HTTP status code for the response
 */
typedef struct http_funcs {
    void* (*realloc_scratch)(void* opaque, void* ptr, int size);
    void (*body)(void* opaque, const char* data, int size);
    void (*header)(void* opaque, const char* key, int nkey, const char* value, int nvalue);
    void (*code)(void* opqaue, int code);
} http_funcs;

typedef struct http_roundtripper {
    struct http_funcs funcs;
    void *opaque;
    char *scratch;
    int code;
    int parsestate;
    int contentlength;
    int state;
    int nscratch;
    int nkey;
    int nvalue;
    int chunked;
} http_roundtripper;

/**
 * Initializes a rountripper with the specified response functions. This must
 * be called before the rt object is used.
 */
void http_init(struct http_roundtripper* rt, struct http_funcs, void* opaque);

/**
 * Frees any scratch memory allocated during parsing.
 */
void http_free(struct http_roundtripper* rt);

/**
 * Parses a block of HTTP response data. Returns zero if the parser reached the
 * end of the response, or an error was encountered. Use http_iserror to check
 * for the presence of an error. Returns non-zero if more data is required for
 * the response.
 */
int http_data(struct http_roundtripper* rt, const char* data, int size, int* read);

/**
 * Returns non-zero if a completed parser encounted an error. If http_data did
 * not return non-zero, the results of this function are undefined.
 */
int http_iserror(struct http_roundtripper* rt);

#if defined(__cplusplus)
}
#endif

#endif
