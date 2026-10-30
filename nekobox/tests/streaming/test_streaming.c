/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <zephyr/mp4.h>
#include <dlna/dlna.h>
#include "player_file.h"
#include "fetch.h"

static atomic_t request_seq = 1;
static _Atomic uint64_t synced;

bool pl_req_changed(uint32_t seq) { return seq != (uint32_t)atomic_get(&request_seq); }
void fs_file_t_init(struct fs_file_t *f) { f->fd = -1; f->size = 0; }
int fs_open(struct fs_file_t *f, const char *path, int flags)
{
	struct stat st;
	int mode = flags & FS_O_WRITE ? O_WRONLY : O_RDONLY;

	if (flags & FS_O_CREATE) { mode |= O_CREAT; }
	f->fd = open(path, mode, 0600);
	if (f->fd < 0) { return -errno; }
	assert(fstat(f->fd, &st) == 0);
	f->size = st.st_size;
	return 0;
}
int fs_close(struct fs_file_t *f)
{
	int ret = f->fd < 0 ? 0 : close(f->fd);
	f->fd = -1;
	return ret < 0 ? -errno : 0;
}
int fs_seek(struct fs_file_t *f, off_t off, int whence)
{
	if (off < 0 || (uint64_t)off > f->size) { return -EINVAL; }
	return lseek(f->fd, off, whence) < 0 ? -errno : 0;
}
ssize_t fs_read(struct fs_file_t *f, void *buf, size_t len)
{
	off_t off = lseek(f->fd, 0, SEEK_CUR);
	ssize_t n;
	assert(off >= 0);
	if (len > f->size - off) { len = f->size - off; }
	n = read(f->fd, buf, len);
	return n < 0 ? -errno : n;
}
ssize_t fs_write(struct fs_file_t *f, const void *buf, size_t len)
{
	ssize_t n = write(f->fd, buf, len);
	if (n > 0) { f->size += n; }
	return n < 0 ? -errno : n;
}
int fs_sync(struct fs_file_t *f)
{
	int ret = fsync(f->fd);
	if (ret == 0) { atomic_store(&synced, f->size); }
	return ret < 0 ? -errno : 0;
}
int fs_unlink(const char *path) { return unlink(path) == 0 ? 0 : -errno; }

struct transfer {
	pthread_mutex_t lock;
	uint64_t available, total;
	int state;
	atomic_t cancel;
	char url[DLNA_URI_SIZE], path[512];
};

static void progress(void *ctx, uint64_t available, uint64_t total)
{
	struct transfer *t = ctx;
	assert(available <= atomic_load(&synced));
	pthread_mutex_lock(&t->lock);
	assert(available >= t->available);
	t->available = available;
	t->total = total;
	pthread_mutex_unlock(&t->lock);
}
static uint64_t avail(void *ctx, int *state)
{
	struct transfer *t = ctx;
	uint64_t n;
	pthread_mutex_lock(&t->lock);
	n = t->available;
	*state = t->state;
	pthread_mutex_unlock(&t->lock);
	return n;
}
static void *download(void *ctx)
{
	struct transfer *t = ctx;
	int ret = dlna_fetch(t->url, t->path, &t->cancel, progress, t);
	pthread_mutex_lock(&t->lock);
	t->state = ret == 0 ? 1 : ret;
	pthread_mutex_unlock(&t->lock);
	return NULL;
}

struct server {
	int fd;
	uint8_t *data;
	size_t size;
	const char *mode;
	atomic_t released;
};
static bool send_bytes(int fd, const void *data, size_t n)
{
	const uint8_t *p = data;
	while (n) {
		ssize_t ret = send(fd, p, n, MSG_NOSIGNAL);
		if (ret <= 0) { return false; }
		p += ret; n -= ret;
	}
	return true;
}
static void *serve(void *ctx)
{
	struct server *s = ctx;
	char req[DLNA_URI_SIZE + 512], header[DLNA_URI_SIZE + 128];
	int fd = accept(s->fd, NULL, NULL);
	size_t got = 0;
	ssize_t n;
	assert(fd >= 0);
	do {
		n = recv(fd, req + got, sizeof(req) - 1 - got, 0);
		assert(n > 0);
		got += n; req[got] = '\0';
	} while (!strstr(req, "\r\n\r\n"));
	if (!strcmp(s->mode, "longurl")) {
		assert(strstr(req, "&end=present HTTP/1.1\r\n"));
		assert(strlen(req) > 3000);
	}
	if (!strcmp(s->mode, "redirect")) {
		const char *r = "HTTP/1.1 302 Found\r\nLocation: /video\r\nContent-Length: 0\r\n\r\n";
		send_bytes(fd, r, strlen(r)); close(fd);
		fd = accept(s->fd, NULL, NULL); assert(fd >= 0);
		assert(recv(fd, req, sizeof(req), 0) > 0);
	}
	if (!strcmp(s->mode, "error")) {
		const char *r = "HTTP/1.1 500 Failed\r\nContent-Length: 0\r\n\r\n";
		send_bytes(fd, r, strlen(r)); goto out;
	}
	if (!strcmp(s->mode, "badchunk")) {
		const char *r = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nxyz\r\n";
		send_bytes(fd, r, strlen(r)); goto out;
	}
	bool chunked = !strcmp(s->mode, "chunked");
	if (chunked) {
		snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nTransfer-Encoding: cHuNkEd\r\n\r\n");
	} else if (!strcmp(s->mode, "close")) {
		snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n");
	} else {
		snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n", s->size);
	}
	if (!send_bytes(fd, header, strlen(header))) { goto out; }
	for (size_t off = 0; off < s->size;) {
		size_t n = s->size - off > 4096 ? 4096 : s->size - off;
		if (off >= 128 * 1024 && !strcmp(s->mode, "truncated")) { break; }
		while (off >= 128 * 1024 && !atomic_get(&s->released)) { usleep(1000); }
		if (chunked) {
			snprintf(header, sizeof(header), "%zx;part=yes\r\n", n);
			if (!send_bytes(fd, header, strlen(header))) { goto out; }
		}
		if (!send_bytes(fd, s->data + off, n)) { goto out; }
		if (chunked && !send_bytes(fd, "\r\n", 2)) { goto out; }
		off += n;
	}
	if (chunked) {
		const char *end = "0\r\nX-Complete: yes\r\n\r\n";
		send_bytes(fd, end, strlen(end));
	}
 out:
	close(fd); close(s->fd); return NULL;
}

struct reading { struct reader *r; uint64_t off; size_t size; uint8_t *buf; int ret; atomic_t done; };
static void *read_wait(void *ctx)
{
	struct reading *r = ctx;
	r->ret = pl_file_read(r->r, r->off, r->buf, r->size);
	atomic_set(&r->done, 1);
	return NULL;
}

static uint8_t *load(const char *path, size_t *size)
{
	struct stat st;
	FILE *f = fopen(path, "rb"); assert(f);
	assert(fstat(fileno(f), &st) == 0);
	*size = st.st_size;
	uint8_t *data = malloc(*size); assert(data);
	assert(fread(data, 1, *size, f) == *size); fclose(f); return data;
}

static void test_transfer(const char *dir, const char *mode, uint8_t *data, size_t size, bool tail)
{
	struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
	socklen_t len = sizeof(addr);
	struct server s = {.data = data, .size = size, .mode = mode};
	struct transfer t = {.lock = PTHREAD_MUTEX_INITIALIZER};
	pthread_t server_thread, fetch_thread;
	int state;

	atomic_set(&request_seq, 1);
	atomic_store(&synced, 0);
	s.fd = socket(AF_INET, SOCK_STREAM, 0); assert(s.fd >= 0);
	assert(bind(s.fd, (void *)&addr, sizeof(addr)) == 0);
	assert(getsockname(s.fd, (void *)&addr, &len) == 0);
	assert(listen(s.fd, 2) == 0);
	snprintf(t.url, sizeof(t.url), "http://127.0.0.1:%u/video", ntohs(addr.sin_port));
	if (!strcmp(mode, "longurl")) {
		strcat(t.url, "?token=");
		size_t used = strlen(t.url);

		memset(t.url + used, 'x', 3000);
		strcpy(t.url + used + 3000, "&end=present");
	}
	snprintf(t.path, sizeof(t.path), "%s/download.mp4", dir);
	assert(pthread_create(&server_thread, NULL, serve, &s) == 0);
	assert(pthread_create(&fetch_thread, NULL, download, &t) == 0);
	bool error = !strcmp(mode, "error") || !strcmp(mode, "badchunk") || !strcmp(mode, "truncated");
	if (error) {
		pthread_join(fetch_thread, NULL);
		avail(&t, &state); assert(state < 0);
		if (!strcmp(mode, "truncated")) {
			assert(access(t.path, F_OK) == 0); /* partial file belongs to the reader */
		}
	} else {
		uint64_t available = 0;
		for (unsigned i = 0; i < 5000; i++) {
			available = avail(&t, &state);
			assert(state == 0);
			if (available >= 128 * 1024) { break; }
			usleep(1000);
		}
		assert(available >= 128 * 1024 && available < size);
		struct player_growing g = {.avail = avail, .ctx = &t, .max_width = 640, .max_height = 360};
		pthread_mutex_lock(&t.lock); g.total = t.total; pthread_mutex_unlock(&t.lock);
		assert(player_growing_ready(t.path, &g) == (tail ? -EAGAIN : 0));
		struct reader r;
		assert(pl_reader_open(&r, t.path, 4096) == 0);
		pl_reader_growing(&r, &g);
		if (!tail) {
			uint64_t movie_size;
			struct mp4 m;
			struct mp4_iter it;
			struct mp4_sample sample;
			assert(pl_reader_movie_size(&r, &movie_size) == 0);
			assert(mp4_open(&m, pl_file_read, &r, movie_size) == 0);
			assert(m.video.codec == MP4_CODEC_H264 && m.audio.codec == MP4_CODEC_AAC);
			assert(mp4_iter_init(&m.video, &it) == 0);
			assert(mp4_next(&m.video, &it, &sample) == 0);
			uint8_t *frame = malloc(sample.size); assert(frame);
			assert(pl_file_read(&r, sample.offset, frame, sample.size) == 0);
			assert(memcmp(frame, data + sample.offset, sample.size) == 0);
			free(frame); mp4_close(&m);
		}
		r.opening = false; r.seq = 1;
		uint8_t buf[8192];
		struct reading rd = {.r = &r, .off = size - sizeof(buf), .size = sizeof(buf), .buf = buf};
		pthread_t reading;
		assert(pthread_create(&reading, NULL, read_wait, &rd) == 0);
		usleep(50000); assert(!atomic_get(&rd.done));
		if (!strcmp(mode, "cancel")) {
			atomic_set(&request_seq, 2); /* Pause/seek/close interrupts an outstanding read. */
			pthread_join(reading, NULL); assert(rd.ret == -ECANCELED);
			atomic_set(&t.cancel, 1);
			pthread_join(fetch_thread, NULL);
			avail(&t, &state); assert(state == -ECANCELED);
			atomic_set(&s.released, 1);
		} else {
			atomic_set(&s.released, 1);
			pthread_join(reading, NULL); assert(rd.ret == 0);
			assert(memcmp(buf, data + rd.off, sizeof(buf)) == 0);
			pthread_join(fetch_thread, NULL);
			assert(avail(&t, &state) == size && state == 1);
			assert(player_growing_ready(t.path, &g) == 0);
			assert(pl_file_read(&r, size, buf, 1) == -EIO); /* terminal short read never hangs */
		}
		pl_reader_close(&r);
	}
	pthread_join(server_thread, NULL);
	pthread_mutex_destroy(&t.lock);
	unlink(t.path);
	printf("PASS HTTP %s%s\n", mode, tail ? " (moov at end)" : "");
}

static uint64_t fixed_avail(void *ctx, int *state)
{
	const uint64_t *n = ctx;
	*state = 1; return *n;
}

static void test_reader_failure(const char *dir)
{
	char path[512];
	struct transfer t = {.lock = PTHREAD_MUTEX_INITIALIZER};
	struct player_growing g = {.avail = avail, .ctx = &t};
	struct reader r;
	uint8_t buf[8];
	pthread_t thread;

	snprintf(path, sizeof(path), "%s/fast.mp4", dir);
	atomic_set(&request_seq, 1);
	assert(pl_reader_open(&r, path, 64) == 0);
	assert(pl_file_read(&r, 4, buf, 4) == 0 && memcmp(buf, "ftyp", 4) == 0);
	pl_reader_close(&r);
	assert(pl_reader_open(&r, path, 64) == 0);
	pl_reader_growing(&r, &g);
	/* Unpublished bytes must stay hidden even if they already exist on the card. */
	assert(pl_file_read(&r, 0, buf, 8) == -EAGAIN);
	r.opening = false; r.seq = 1;
	struct reading rd = {.r = &r, .size = sizeof(buf), .buf = buf};
	assert(pthread_create(&thread, NULL, read_wait, &rd) == 0);
	usleep(50000); assert(!atomic_get(&rd.done));
	pthread_mutex_lock(&t.lock); t.state = -ECONNRESET; pthread_mutex_unlock(&t.lock);
	pthread_join(thread, NULL);
	assert(rd.ret == -ECONNRESET);
	pl_reader_close(&r);
	pthread_mutex_destroy(&t.lock);
	puts("PASS local reader, unpublished data, nonblocking open, failure while waiting");
}

static void test_headers(const char *dir)
{
	char path[512];
	uint8_t extended[] = {0,0,0,1,'m','o','o','v',0,0,0,0,0,0,0,16};
	snprintf(path, sizeof(path), "%s/boxes.mp4", dir);
	FILE *f = fopen(path, "wb"); assert(f);
	assert(fwrite(extended, 1, sizeof(extended), f) == sizeof(extended)); fclose(f);
	uint64_t n = sizeof(extended);
	struct player_growing g = {.avail = fixed_avail, .ctx = &n};
	assert(player_growing_ready(path, &g) == 0);
	struct reader r; uint64_t size;
	assert(pl_reader_open(&r, path, 64) == 0); pl_reader_growing(&r, &g);
	assert(pl_reader_movie_size(&r, &size) == 0 && size == 16);
	assert(pl_file_read(&r, UINT64_MAX, extended, 2) == -EOVERFLOW);
	pl_reader_close(&r);
	extended[15] = 7;
	f = fopen(path, "wb"); assert(f); fwrite(extended, 1, sizeof(extended), f); fclose(f);
	assert(player_growing_ready(path, &g) == -EINVAL);
	unlink(path);
	puts("PASS extended and malformed MP4 headers, offset overflow");
}

static void test_resolution(const char *dir)
{
	const char *sizes[] = {"640x360", "360x640", "642x360", "640x362", "480x480", "1280x720", "1920x1080"};
	char path[512];
	struct stat st;
	uint64_t n;
	struct player_growing g = {.avail = fixed_avail, .ctx = &n, .max_width = 640, .max_height = 360};

	for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s.mp4", dir, sizes[i]);
		assert(stat(path, &st) == 0);
		n = st.st_size;
		assert(player_growing_ready(path, &g) == (i < 2 ? 0 : -E2BIG));
	}
	/* The limit belongs to a cast, so other player callers can leave it unset. */
	g.max_width = g.max_height = 0;
	assert(player_growing_ready(path, &g) == 0);
	puts("PASS MP4 360p size limit, portrait, oversized headers and unlimited caller");
}
int main(int argc, char **argv)
{
	assert(argc == 2);
	signal(SIGPIPE, SIG_IGN);
	char path[512]; size_t size;
	snprintf(path, sizeof(path), "%s/fast.mp4", argv[1]);
	uint8_t *data = load(path, &size); assert(size > 256 * 1024);
	const char *modes[] = {"length", "longurl", "redirect", "chunked", "close", "truncated", "badchunk", "error", "cancel"};
	for (size_t i = 0; i < sizeof(modes)/sizeof(modes[0]); i++) {
		test_transfer(argv[1], modes[i], data, size, false);
	}
	free(data);
	snprintf(path, sizeof(path), "%s/tail.mp4", argv[1]); data = load(path, &size);
	test_transfer(argv[1], "length", data, size, true); free(data);
	test_headers(argv[1]);
	test_reader_failure(argv[1]);
	test_resolution(argv[1]);
	puts("PASS (0 failures)");
	return 0;
}
