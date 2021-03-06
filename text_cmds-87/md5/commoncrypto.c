/* Generic CommonDigest wrappers to match the semantics of libmd. */

#include <dispatch/dispatch.h>
#include <assumes.h>
#include <errno.h>
#include <fcntl.h>

#include "commoncrypto.h"

char *
Digest_End(CCDigestRef ctx, char *buf)
{
	static const char hex[] = "0123456789abcdef";
	uint8_t digest[32]; // SHA256 is the biggest
	size_t i, length;

	(void)osx_assumes_zero(CCDigestFinal(ctx, digest));
	length = CCDigestOutputSize(ctx);
	osx_assert(length <= sizeof(digest));
	for (i = 0; i < length; i++) {
		buf[i+i] = hex[digest[i] >> 4];
		buf[i+i+1] = hex[digest[i] & 0x0f];
	}
	buf[i+i] = '\0';
	return buf;
}

char *
Digest_Data(CCDigestAlg algorithm, const void *data, size_t len, char *buf)
{
	CCDigestCtx ctx;

	(void)osx_assumes_zero(CCDigestInit(algorithm, &ctx));
	(void)osx_assumes_zero(CCDigestUpdate(&ctx, data, len));
	return Digest_End(&ctx, buf);
}

char *
Digest_File(CCDigestAlg algorithm, const char *filename, char *buf)
{
	int fd;
	__block CCDigestCtx ctx;
	dispatch_queue_t queue;
	dispatch_semaphore_t sema;
	dispatch_io_t io;
	__block int s_error = 0;

	/* dispatch_io_create_with_path requires an absolute path */
	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		return NULL;
	}

	(void)fcntl(fd, F_NOCACHE, 1);

	(void)osx_assumes_zero(CCDigestInit(algorithm, &ctx));

	queue = dispatch_queue_create("com.apple.mtree.io", NULL);
	osx_assert(queue);
	sema = dispatch_semaphore_create(0);
	osx_assert(sema);

	io = dispatch_io_create(DISPATCH_IO_STREAM, fd, queue, ^(int error) {
		if (error != 0) {
			s_error = error; 
		}
		(void)close(fd);
		(void)dispatch_semaphore_signal(sema);
	});
	osx_assert(io);
	dispatch_io_read(io, 0, SIZE_MAX, queue, ^(__unused bool done, dispatch_data_t data, int error) {
		if (data != NULL) {
			(void)dispatch_data_apply(data, ^(__unused dispatch_data_t region, __unused size_t offset, const void *buffer, size_t size) {
				(void)osx_assumes_zero(CCDigestUpdate(&ctx, buffer, size));
				return (bool)true;
			});
		}

		if (error != 0) {
			s_error = error;
		}
	});
	dispatch_release(io); // it will close on its own

	(void)dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);

	dispatch_release(queue);
	dispatch_release(sema);

	if (s_error != 0) {
		errno = s_error;
		return NULL;
	}

	return Digest_End(&ctx, buf);
}
