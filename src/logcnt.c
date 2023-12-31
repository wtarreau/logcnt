#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <limits.h>
#include <time.h>

#define MAX_THREADS 64
#define MAX_SENDERS 1024

// window size
#define WINSZ (sizeof(long)*8)

struct thread_data {
	pthread_t pth;
	int id;           /* thread num    */
	int fd;           /* socket        */
	long long count;  /* rcvd messages */
	long long bytes;  /* rcvd bytes    */
	long long loops;  /* number of loops back */
	long long losses; /* sum of holes */
	long long dups;   /* sum of duplicates */
} __attribute__((aligned(64)));

struct sender_data {
	pthread_rwlock_t lock;
	unsigned int addr_hash;
	unsigned int head;       /* highest counter seen */
	unsigned long prev_seen; /* 1 bit per rx before hctr, covers hctr-64 to hctr-1 */
} __attribute__((aligned(64)));

struct sender_data sender_data[MAX_SENDERS];
static int cfg_count_only;


void die(int err, const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(err);
}

void die_err(int err, const char *msg)
{
	perror(msg);
	exit(err);
}

/* read uint between ptr and end. Stops at the first non-digit */
unsigned int read_uint(const char *ptr, const char *end)
{
	unsigned int i = 0;
	unsigned int j, k;

	while (ptr < end) {
		j = *ptr - '0';
		k = i * 10;
		if (j > 9)
			break;
		i = k + j;
		ptr++;
	}
	return i;
}

/* mix input data in a xorshift */
unsigned int small_hash(const void *input, int len)
{
	const void *end = input + len;
	unsigned int x = ~0U/3; // set half bits
	unsigned int c;

	while (input + 4 <= end) {
#if defined(__x86_64__) || defined(__i386__) || defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined (__aarch64__) || defined(__ARM_ARCH_8A)
		c = *(unsigned int *)input;
#else
		c = ((unsigned char*)input)[0] +
		    ((unsigned char*)input)[1] * 256 +
		    ((unsigned char*)input)[2] * 65536 +
		    ((unsigned char*)input)[3] * 16777216;
#endif
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		x ^= c;
		input += 4;
	}

	while (input < end) {
		c = ((unsigned char*)input)[0];
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		x ^= c;
		input += 1;
	}

	return x;
}

void *udprx(void *arg)
{
	struct thread_data *td = arg;
	struct sender_data *sd;
	int fd = td->fd;
	ssize_t len;
	char buffer[65536];
	struct sockaddr_in addr;
	socklen_t salen;
	char *p1 = buffer, *p0, *e;
	unsigned int hash, bucket;
	unsigned int counter;

	while (1) {
#if defined(__linux__)
		if (cfg_count_only) {
			len = recv(fd, buffer, 0, MSG_TRUNC);
		} else
#endif
		{
			salen = sizeof(addr);
			len = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&addr, &salen);
		}
		if (len < 0)
			break;

		td->count++;
		td->bytes += len;

		if (cfg_count_only)
			continue;

		/* skip priority */
		p1 = buffer;
		e = buffer + len;
		if (*p1 == '<') {
			if (p1[1] == '>')
				p1 += 2;
			else if (p1[2] == '>')
				p1 += 3;
			else if (p1[3] == '>')
				p1 += 4;
			else if (p1[4] == '>')
				p1 += 5;
			else {
				while (p1 < e && *p1 != ' ')
					p1++;
			}
			if (*p1 == ' ')
				p1++;
		}

		/* we're now on first char after the space after priority. The format
		 * is |mon dd hh:mm:ss[.*]|
		 *      3   2     8  + 2 spaces = 15 chars
		 */
		p1 += 3 + 1 + 2 + 1 + 8;
		if (p1 >= e)
			continue;

		/* skip optional subsecond chars */
		while (p1 < e && *p1 != ' ')
			p1++;

		if (*p1 == ' ')
			p1++;

		/* now p1 is on the hostname. If it ends with a colon it's the tag
		 * and we have no host name.
		 */
		for (p0 = p1; p1 < e && *p1 != ' '; p1++)
			;

		if (*(p1 - 1) == ':') {
			/* no hostname, hash src IP */
			hash = small_hash(&addr,  salen);
		} else {
			/* hostname, hash it */
			hash = small_hash(p0, p1 - p0);

			/* now also skip tag */
			if (*p1 == ' ')
				p1++;

			for (p0 = p1; p1 < e && *p1 != ' '; p1++)
				;
		}

		/* we're now past the tag */
		if (*p1 == ' ')
			p1++;

		/* the beginning of the message is expected to be a counter. */
		counter = read_uint(p1, e);

		bucket = hash;
		while (bucket >= MAX_SENDERS)
			bucket = (bucket % MAX_SENDERS) ^ (bucket / MAX_SENDERS);

		sd = &sender_data[bucket];

		pthread_rwlock_wrlock(&sd->lock);

		if (sd->addr_hash != hash) {
			/* hash collision, replace the entry under a write lock */
			sd->addr_hash = hash;
			sd->head = counter;
			sd->prev_seen = 0UL; /* assume no predecessors were received */
		}

		if (counter + WINSZ < sd->head) {
			/* rollback out of window */
			if (counter >= sd->head - 2*WINSZ) {
				/* still within previous window, we can keep some bits,
				 * just consider this is the oldest possible packet.
				 */
				sd->prev_seen <<= (sd->head - counter + WINSZ);
				if (sd->prev_seen & 1)
					__atomic_add_fetch(&td->dups, 1, __ATOMIC_RELAXED);
			} else {
				/* even out of previous window */
				sd->prev_seen = 0;
			}
			sd->prev_seen |= 1;
			sd->head = counter + WINSZ;
			__atomic_add_fetch(&td->loops, 1, __ATOMIC_RELAXED);
		} else if (counter < sd->head) {
			/* still within current window */
			unsigned long mask = 1UL << (counter - sd->head + WINSZ);

			if (sd->prev_seen & mask)
				__atomic_add_fetch(&td->dups, 1, __ATOMIC_RELAXED);
			sd->prev_seen |= mask;
		} else if (counter == sd->head && sd->head) {
			__atomic_add_fetch(&td->dups, 1, __ATOMIC_RELAXED);
		} else if (counter > sd->head) {
			if (counter < sd->head + WINSZ) {
				/* still within next window, we can keep some bits
				 * and count the lost ones (those we're going to drop
				 * that still have a zero bit).
				 */
				unsigned long mask = 1UL << (sd->head + WINSZ - counter);
				unsigned long drop;
				int lost;

				drop = ~(~0UL << (counter - sd->head)) & ~sd->prev_seen;
				sd->prev_seen >>= counter - sd->head;

				if (sd->prev_seen & mask)
					__atomic_add_fetch(&td->dups, 1, __ATOMIC_RELAXED);
				sd->prev_seen |= mask; // count prev head
				lost = __builtin_popcountl(drop);
				if (lost && sd->head >= WINSZ)
					__atomic_add_fetch(&td->losses, lost, __ATOMIC_RELAXED);
			} else {
				/* even out of next window: all unreceived are lost */
				int lost;

				lost = __builtin_popcountl(~sd->prev_seen);
				if (counter >= sd->head + 2*WINSZ)
					lost += counter - 2*WINSZ - sd->head;
				sd->prev_seen = 0;
			}
			sd->head = counter;
		}

		pthread_rwlock_unlock(&sd->lock);
	}

	close(fd);
	pthread_exit(NULL);
}


int main(int argc, char **argv)
{
	struct thread_data td[MAX_THREADS];
	struct sockaddr_in saddr;
	const char *prog = *argv;
	long long total_msgs = 0;
	long long total_bytes = 0;
	long long total_losses = 0;
	long long total_loops = 0;
	long long total_dups = 0;
	unsigned int tot_time = 0;
	int cfg_absdate = 0;
	int cfg_maxidle = 0;
	int cfg_rcvbuf = 0;
	int num_threads = 1;
	int port = 514;
	int i, fd;
	int idle = 0;

	setlinebuf(stdout);

	--argc; ++argv;

	while (argc && **argv == '-') {
		if (argc > 1 && strcmp(*argv, "-t") == 0) {
			num_threads = atoi(argv[1]);
			if (num_threads < 1 || num_threads > MAX_THREADS)
				die(1, "Invalid number of threads\n");
			argc--; argv++;
		}
		else if (argc > 1 && strcmp(*argv, "-p") == 0) {
			port = atoi(argv[1]);
			if (port < 0 || port > 65535)
				die(1, "Invalid port number\n");
			argc--; argv++;
		}
		else if (argc > 1 && strcmp(*argv, "-i") == 0) {
			cfg_maxidle = atoi(argv[1]);
			argc--; argv++;
		}
		else if (argc > 1 && strcmp(*argv, "-b") == 0) {
			cfg_rcvbuf = atoi(argv[1]);
			argc--; argv++;
		}
		else if (strcmp(*argv, "-a") == 0) {
			cfg_absdate = 1;
		}
		else if (strcmp(*argv, "-c") == 0) {
			cfg_count_only = 1;
		}
		else
			break;
		argc--;
		argv++;
	}

	if (argc > 0) {
		fprintf(stderr,
			"Usage: %s [options]\n"
			"  -t <threads> : set receiving threads count (def: 1)\n"
			"  -p <port>    : set listening port (def: 514)\n"
			"  -i <seconds> : set idle duration before automatic reset\n"
			"  -b <bytes>   : set each socket's rcvbuf to this value\n"
			"  -a           : use absolute date\n"
			"  -c           : only count received msgs, do not analyze them\n"
			"\n", prog);
		exit(1);
	}

	for (i = 0; i < MAX_SENDERS; i++) {
		memset(&sender_data[i], 0, sizeof(sender_data[i]));
		pthread_rwlock_init(&sender_data[i].lock, NULL);
	}

	memset(&saddr, 0, sizeof(saddr));
	saddr.sin_family = AF_INET;
	saddr.sin_addr.s_addr = htonl(INADDR_ANY);
	saddr.sin_port = htons(port);

	for (i = 0; i < num_threads; i++) {
		const int one = 1;

		memset(&td[i], 0, sizeof(td[i]));
		fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (fd < 0)
			die_err(1, "socket");

		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));

		if (cfg_rcvbuf)
			setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &cfg_rcvbuf, sizeof(cfg_rcvbuf));

		if (bind(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
			die_err(1, "bind");

		td[i].id = i;
		td[i].fd = fd;
	}

	for (i = 0; i < num_threads; i++)
		pthread_create(&td[i].pth, NULL, udprx, (void *)&td[i]);

	printf("# time    pct  totmsg    bytes   losses dups loops |  msg/s   bytes/s loss/s dups/s loops/s\n");
	while (1) {
		long long prev_msgs = total_msgs;
		long long prev_bytes = total_bytes;
		long long prev_losses = total_losses;
		long long prev_loops = total_loops;
		long long prev_dups = total_dups;
		struct timeval now, next;

		/* prepare to sleep till next round second */
		gettimeofday(&now, NULL);

		next.tv_sec = 0;
		next.tv_usec = 1000000 - now.tv_usec;
		if (next.tv_usec >= 1000000) {
			next.tv_usec = 0;
			next.tv_sec = 1;
		}

		select(0, NULL, NULL, NULL, &next);

		tot_time++;
		total_msgs = total_bytes = total_losses = total_loops = total_dups = 0;
		for (i = 0; i < num_threads; i++) {
			total_msgs += td[i].count;
			total_bytes += td[i].bytes;
			total_losses += td[i].losses;
			total_loops += td[i].loops;
			total_dups += td[i].dups;
		}

		printf("%6u %6.2f %lld %lld %lld %lld %lld | %lld %lld %lld %lld %lld\n",
		       cfg_absdate ? (unsigned int)now.tv_sec : tot_time,
		       total_msgs ? total_msgs * 100.0 / (total_msgs + total_losses) : 0.0,
		       total_msgs, total_bytes, total_losses, total_dups, total_loops,
		       total_msgs-prev_msgs, total_bytes-prev_bytes,
		       total_losses-prev_losses, total_dups-prev_dups, total_loops-prev_loops);

		if (total_msgs == prev_msgs) {
			idle++;
			if (cfg_maxidle && idle >= cfg_maxidle) {
				for (i = 0; i < num_threads; i++) {
					prev_msgs   = total_msgs   = td[i].count = 0;
					prev_bytes  = total_bytes  = td[i].bytes = 0;
					prev_losses = total_losses = td[i].losses = 0;
					prev_loops  = total_loops  = td[i].loops = 0;
					prev_dups   = total_dups   = td[i].dups = 0;
				}
			}
		}
		else
			idle = 0;
	}

	close(fd);
	return 0;
}
