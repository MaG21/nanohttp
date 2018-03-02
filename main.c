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

static const unsigned char http_chunk_state[] = {
/*     *    LF    CR    HEX */
    0xC1, 0xC1, 0xC1,    1, /* s0: initial hex char */
    0xC1, 0xC1,    2, 0x81, /* s1: additional hex chars, followed by CR */
    0xC1, 0x83, 0xC1, 0xC1, /* s2: trailing LF */
    0xC1, 0xC1,    4, 0xC1, /* s3: CR after chunk block */
    0xC1, 0xC0, 0xC1, 0xC1, /* s4: LF after chunk block */
};

static unsigned char http_header_state[] = {
/*     *    \t    \n   \r    ' '     ,     :   PAD */
    0x80,    1, 0xC1, 0xC1,    1, 0x80, 0x80, 0xC1, /* state 0: HTTP version */
    0x81,    2, 0xC1, 0xC1,    2,    1,    1, 0xC1, /* state 1: Response code */
    0x82, 0x82,    4,    3, 0x82, 0x82, 0x82, 0xC1, /* state 2: Response reason */
    0xC1, 0xC1,    4, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, /* state 3: HTTP version newline */
    0x84, 0xC1, 0xC0,    5, 0xC1, 0xC1,    6, 0xC1, /* state 4: Start of header field */
    0xC1, 0xC1, 0xC0, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, /* state 5: Last CR before end of header */
    0x87,    6, 0xC1, 0xC1,    6, 0x87, 0x87, 0xC1, /* state 6: leading whitespace before header value */
    0x87, 0x87, 0xC4,   10, 0x87, 0x88, 0x87, 0xC1, /* state 7: header field value */
    0x87, 0x88,    6,    9, 0x88, 0x88, 0x87, 0xC1, /* state 8: Split value field value */
    0xC1, 0xC1,    6, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, /* state 9: CR after split value field */
    0xC1, 0xC1, 0xC4, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, /* state 10:CR after header value */
};

int
http_parse_header_char(int* state, char ch)
{
    int newstate, code = 0;
    switch (ch) {
    case '\t': code = 1; break;
    case '\n': code = 2; break;
    case '\r': code = 3; break;
    case  ' ': code = 4; break;
    case  ',': code = 5; break;
    case  ':': code = 6; break;
    }

    newstate = http_header_state[*state * 8 + code];
    *state = (newstate & 0xF);

    switch (newstate) {
    case 0xC0: return http_header_status_done;
    case 0xC1: return http_header_status_done;
    case 0xC4: return http_header_status_store_keyvalue;
    case 0x80: return http_header_status_version_character;
    case 0x81: return http_header_status_code_character;
    case 0x82: return http_header_status_status_character;
    case 0x84: return http_header_status_key_character;
    case 0x87: return http_header_status_value_character;
    case 0x88: return http_header_status_value_character;
    }

    return http_header_status_continue;
}

static void
append_body(struct http_roundtripper* rt, const char* data, int ndata)
{
	rt->funcs.body(rt->opaque, data, ndata);
}

static void
grow_scratch(struct http_roundtripper* rt, int size)
{
	if (rt->nscratch >= size)
		return;

	if (size < 64)
		size = 64;
	int nsize = (rt->nscratch * 3) / 2;
	if (nsize < size)
		nsize = size;

	rt->scratch = (char*)rt->funcs.realloc_scratch(rt->opaque, rt->scratch, nsize);
	rt->nscratch = nsize;
}

static int
min(int a, int b)
{
	return a > b ? b : a;
}

enum http_roundtripper_state {
	http_roundtripper_header,
	http_roundtripper_chunk_header,
	http_roundtripper_chunk_data,
	http_roundtripper_raw_data,
	http_roundtripper_unknown_data,
	http_roundtripper_close,
	http_roundtripper_error,
};

int
http_parse_chunked(int* state, int *size, char ch)
{
    int newstate, code = 0;
    switch (ch) {
    case '\n': code = 1; break;
    case '\r': code = 2; break;
    case '0': case '1': case '2': case '3':
    case '4': case '5': case '6': case '7':
    case '8': case '9': case 'a': case 'b':
    case 'c': case 'd': case 'e': case 'f':
    case 'A': case 'B': case 'C': case 'D':
    case 'E': case 'F': code = 3; break;
    }

    newstate = http_chunk_state[*state * 4 + code];
    *state = (newstate & 0xF);

    switch (newstate) {
    case 0xC0:
        return *size != 0;

    case 0xC1: /* error */
        *size = -1;
        return 0;

    case 0x01: /* initial char */
        *size = 0;
        /* fallthrough */
    case 0x81: /* size char */
        if (ch >= 'a')
            *size = *size * 16 + (ch - 'a' + 10);
        else if (ch >= 'A')
            *size = *size * 16 + (ch - 'A' + 10);
        else
            *size = *size * 16 + (ch - '0');
        break;

    case 0x83:
        return *size == 0;
    }

    return 1;
}


void
http_init(struct http_roundtripper* rt, struct http_funcs funcs, void* opaque)
{
	rt->funcs = funcs;
	rt->scratch = 0;
	rt->opaque = opaque;
	rt->code = 0;
	rt->parsestate = 0;
	rt->contentlength = -1;
	rt->state = http_roundtripper_header;
	rt->nscratch = 0;
	rt->nkey = 0;
	rt->nvalue = 0;
	rt->chunked = 0;
}

void
http_free(struct http_roundtripper* rt)
{
	if (rt->scratch) {
		rt->funcs.realloc_scratch(rt->opaque, rt->scratch, 0);
		rt->scratch = 0;
	}
}

int
http_data(struct http_roundtripper* rt, const char* data, int size, int* read)
{
	int chunksize;
	const int initial_size = size;
	while (size) {
		switch (rt->state) {
		case http_roundtripper_header:
			switch (http_parse_header_char(&rt->parsestate, *data)) {
			case http_header_status_done:
				rt->funcs.code(rt->opaque, rt->code);
				if (rt->parsestate != 0)
					rt->state = http_roundtripper_error;
				else if (rt->chunked) {
					rt->contentlength = 0;
					rt->state = http_roundtripper_chunk_header;
				} else if (rt->contentlength == 0)
					rt->state = http_roundtripper_close;
				else if (rt->contentlength > 0)
					rt->state = http_roundtripper_raw_data;
				else if (rt->contentlength == -1)
					rt->state = http_roundtripper_unknown_data;
				else
					rt->state = http_roundtripper_error;
				break;

			case http_header_status_code_character:
				rt->code = rt->code * 10 + *data - '0';
				break;

			case http_header_status_key_character:
				grow_scratch(rt, rt->nkey + 1);
				rt->scratch[rt->nkey] = tolower(*data);
				++rt->nkey;
				break;

			case http_header_status_value_character:
				grow_scratch(rt, rt->nkey + rt->nvalue + 1);
				rt->scratch[rt->nkey+rt->nvalue] = *data;
				++rt->nvalue;
				break;

			case http_header_status_store_keyvalue:
				if (rt->nkey == 17 && 0 == strncmp(rt->scratch, "transfer-encoding", rt->nkey))
					rt->chunked = (rt->nvalue == 7 && 0 == strncmp(rt->scratch + rt->nkey, "chunked", rt->nvalue));
				else if (rt->nkey == 14 && 0 == strncmp(rt->scratch, "content-length", rt->nkey)) {
					int ii, end;
					rt->contentlength = 0;
					for (ii = rt->nkey, end = rt->nkey + rt->nvalue; ii != end; ++ii)
						rt->contentlength = rt->contentlength * 10 + rt->scratch[ii] - '0';
				}

				rt->funcs.header(rt->opaque, rt->scratch, rt->nkey, rt->scratch + rt->nkey, rt->nvalue);

				rt->nkey = 0;
				rt->nvalue = 0;
				break;
			}

			--size;
			++data;
			break;

		case http_roundtripper_chunk_header:
			if (!http_parse_chunked(&rt->parsestate, &rt->contentlength, *data)) {
				if (rt->contentlength == -1)
					rt->state = http_roundtripper_error;
				else if (rt->contentlength == 0)
					rt->state = http_roundtripper_close;
				else
					rt->state = http_roundtripper_chunk_data;
			}

			--size;
			++data;
			break;

		case http_roundtripper_chunk_data:
			chunksize = min(size, rt->contentlength);
			append_body(rt, data, chunksize);
			rt->contentlength -= chunksize;
			size -= chunksize;
			data += chunksize;

			if (rt->contentlength == 0) {
				rt->contentlength = 1;
				rt->state = http_roundtripper_chunk_header;
			}

			break;

		case http_roundtripper_raw_data:
			chunksize = min(size, rt->contentlength);
			append_body(rt, data, chunksize);
			rt->contentlength -= chunksize;
			size -= chunksize;
			data += chunksize;

			if (rt->contentlength == 0)
				rt->state = http_roundtripper_close;
			break;

		case http_roundtripper_unknown_data:
			if (size == 0)
				rt->state = http_roundtripper_close;
			else {
				append_body(rt, data, size);
				size -= size;
				data += size;
			}
			break;

		case http_roundtripper_close:
		case http_roundtripper_error:
			break;
		}

		if (rt->state == http_roundtripper_error || rt->state == http_roundtripper_close) {
			if (rt->scratch) {
				rt->funcs.realloc_scratch(rt->opaque, rt->scratch, 0);
				rt->scratch = 0;
			}
			*read = initial_size - size;
			return 0;
		}
	}

	*read = initial_size - size;
	return 1;
}

int
http_iserror(struct http_roundtripper* rt)
{
	return rt->state == http_roundtripper_error;
}
