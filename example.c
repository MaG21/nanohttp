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


#include <string.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <errno.h>

#include "header.h"
#include "chunk.h"
#include "http.h"

#define TRUE  1
#define FALSE 0

extern int errno;

typedef struct HttpResponse {
	char body[5120];
	int code;
	size_t size;
} HttpResponse;

int
connectsocket(const char* host, int port)
{
	int sockfd;
	struct sockaddr_in serv_addr;
	struct hostent * server;
	struct in_addr addr;

	memset(&serv_addr, 0, sizeof (serv_addr));
	memset(&addr, 0, sizeof (addr));


	server = gethostbyname(host);
	if (server == NULL) {
		fprintf(stderr, "Failed to resolve hostname: %s\n", host);
		return -1;
	}

	memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
	serv_addr.sin_port = htons(port);
	serv_addr.sin_family = AF_INET;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		fprintf(stderr, "Failed to get socket fd. Errno: (%s)\n", strerror(errno));
		return -1;
	}

	if ((connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr))) < 0) {
		fprintf(stderr, "Error connecting to hostname: %s, Errno: (%s)\n", host, strerror(errno));
		return -1;
	}

	return sockfd;
}

static void* response_realloc(void* opaque, void* ptr, int size)
{
    return realloc(ptr, size);
}

static void
response_body(void* opaque, const char* data, int size)
{
    HttpResponse* response = (HttpResponse*)opaque;
    printf("DATA: %d\n", size);
    memcpy(&response->body[size], data, size);
    response->size += size;
}

static void
response_header(void* opaque, const char* key, int key_size, const char* value, int value_size)
{
	int ret;
	char buf[1024];
	ret = snprintf(buf, 1023, "%s:%s\n", key, value);

	buf[ret] = 0;
	puts(buf);
}

static void response_code(void* opaque, int code)
{
    HttpResponse* response = (HttpResponse*)opaque;
    response->code = code;
}

static const http_funcs responseFuncs = {
    response_realloc,
    response_body,
    response_header,
    response_code,
};

int
main()
{

    int read;
    int ndata;
    int needmore = TRUE;
    char* data;
    char buffer[1024];
    HttpResponse response;
    http_roundtripper rt;

    int conn = connectsocket("api.yomomma.info", 80);
    if (conn < 0) {
        fprintf(stderr, "Failed to connect socket\n");
        return -1;
    }

    const char request[] = "GET /rates HTTP/1.0\r\nHost: api.yomomma.info\r\n\r\n";
    int len = send(conn, request, sizeof(request) - 1, 0);
    if (len != sizeof(request) - 1) {
        fprintf(stderr, "Failed to send request\n");
        close(conn);
        return -1;
    }

    response.code = 0;
    response.size = 0;

    http_init(&rt, responseFuncs, &response);

    while (needmore) {
        data = buffer;

        ndata = recv(conn, buffer, sizeof(buffer), 0);

        if (ndata <= 0) {
            http_free(&rt);
            close(conn);
            return -1;
        }

        while (needmore && ndata) {
            needmore = http_data(&rt, data, ndata, &read);
            ndata -= read;
            data += read;
        }
    }

    if (http_iserror(&rt)) {
        fprintf(stderr, "Error parsing data\n");
        http_free(&rt);
        close(conn);
        return -1;
    }

    http_free(&rt);
    close(conn);

    printf("Response: %d\n", response.code);
    if (response.size)
	    response.body[response.size] = 0;
        printf("%s\n", &response.body[0]);

    return 0;
}
