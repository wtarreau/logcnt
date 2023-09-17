/* udp-flood - 2011-2023 Willy Tarreau <w@1wt.eu>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <ist.h>

struct errmsg {
	char *msg;
	int size;
	int len;
};

const int zero = 0;
const int one = 1;

const struct ist fields[16][4] = {
	{ IST("zero=0 "),      IST("ZERO=0 "),       IST("[0]=zero "),                       IST("[0]=thirteen_minus_eleven_minus_two "), },
	{ IST("one=1 "),       IST("ONE=1 "),        IST("[1]=zero_plus_one "),              IST("[1]=thirteen_minus_eleven_minus_one "), },
	{ IST("two=2 "),       IST("TWO=2 "),        IST("[2]=one_plus_one "),               IST("[2]=thirteen_minus_eleven "),           },
	{ IST("three=3 "),     IST("THREE=3 "),      IST("[3]=one_plus_two "),               IST("[3]=eleven_minus_seven_minus_one "),    },
	{ IST("four=4 "),      IST("FOUR=4 "),       IST("[4]=two_times_two "),              IST("[4]=eleven_minus_seven "),              },
	{ IST("five=5 "),      IST("FIVE=5 "),       IST("[5]=two_plus_three "),             IST("[5]=thirteen_minus_seven_minus_one "),  },
	{ IST("six=6 "),       IST("SIX=6 "),        IST("[6]=two_times_three "),            IST("[6]=thirteen_minus_seven "),            },
	{ IST("seven=7 "),     IST("SEVEN=7 "),      IST("[7]=two_plus_five "),              IST("[7]=thirteen_minus_five_minus_one "),   },
	{ IST("eight=8 "),     IST("EIGHT=8 "),      IST("[8]=two_power_three "),            IST("[8]=thirteen_minus_five "),             },
	{ IST("nine=9 "),      IST("NINE=9 "),       IST("[9]=three_power_two "),            IST("[9]=thirteen_minus_three_minus_one "),  },
	{ IST("ten=10 "),      IST("TEN=10 "),       IST("[10]=two_times_five "),            IST("[10]=thirteen_minus_three "),           },
	{ IST("eleven=11 "),   IST("ELEVEN=11 "),    IST("[11]=seven_plus_three_plus_one "), IST("[11]=thirteen_minus_two "),             },
	{ IST("twelve=12 "),   IST("TWELVE=12 "),    IST("[12]=three_times_two_power_two "), IST("[12]=thirteen_minus_one "),             },
	{ IST("thirteen=13 "), IST("THIRTEEN=13 "),  IST("[13]=eleven_plus_two "),           IST("[13]=seventeen_minus_two_times_two "),  },
	{ IST("fourteen=14 "), IST("FOURTEEN=14 "),  IST("[14]=seven_times_two "),           IST("[14]=seventeen_minus_three "),          },
	{ IST("fifteen=15 "),  IST("FIFTEEN=15 "),   IST("[15]=five_times_three "),          IST("[15]=seventeen_minus_two "),            },
};

/* fields used by syslog, all must contain the trailing space */
const char *log_prio = "<134> "; // LOG_LOCAL0 + LOG_INFO
const char *log_date = "Sep 17 19:34:00 ";
const char *log_host = "localhost ";
const char *log_tag  = "loggen: ";

static int bitrate;
static int pktrate;
static int count = 1;
static int start_num = 0;
static char *address = "";
unsigned int statistical_prng_state = 0x12345678;

/* Multiply the two 32-bit operands and shift the 64-bit result right 32 bits.
 * This is used to compute fixed ratios by setting one of the operands to
 * (2^32*ratio).
 */
static inline unsigned int mul32hi(unsigned int a, unsigned int b)
{
	return ((unsigned long long)a * b + a) >> 32;
}

/* Xorshift RNGs from http://www.jstatsoft.org/v08/i14/paper.
 * This has a (2^32)-1 period, only zero is never returned.
 */
static inline unsigned int statistical_prng()
{
	unsigned int x = statistical_prng_state;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	return statistical_prng_state = x;
}

/* returns a random number between 0 and <range> - 1 that is evenly distributed
 * over the range.
 */
static inline uint statistical_prng_range(uint range)
{
	return mul32hi(statistical_prng(), range ? range - 1 : 0);
}

char *utoa(unsigned int n, char *buffer, int size)
{
	char *pos;

	pos = buffer + size - 1;
	*pos-- = '\0';

	do {
		*pos-- = '0' + n % 10;
		n /= 10;
	} while (n && pos >= buffer);
	return pos + 1;
}

/* converts str in the form [<ipv4>|<ipv6>|<hostname>]:port to struct sockaddr_storage.
 * Returns < 0 with err set in case of error.
 */
int addr_to_ss(char *str, struct sockaddr_storage *ss, struct errmsg *err)
{
	char *range;

	/* look for the addr/port delimiter, it's the last colon. */
	if ((range = strrchr(str, ':')) == NULL) {
		err->len = snprintf(err->msg, err->size, "Missing port number: '%s'\n", str);
		return -1;
	}

	*range++ = 0;

	memset(ss, 0, sizeof(*ss));

	if (strrchr(str, ':') != NULL) {
		/* IPv6 address contains ':' */
		ss->ss_family = AF_INET6;
		((struct sockaddr_in6 *)ss)->sin6_port = htons(atoi(range));

		if (!inet_pton(ss->ss_family, str, &((struct sockaddr_in6 *)ss)->sin6_addr)) {
			err->len = snprintf(err->msg, err->size, "Invalid server address: '%s'\n", str);
			return -1;
		}
	}
	else {
		ss->ss_family = AF_INET;
		((struct sockaddr_in *)ss)->sin_port = htons(atoi(range));

		if (*str == '*' || *str == '\0') { /* INADDR_ANY */
			((struct sockaddr_in *)ss)->sin_addr.s_addr = INADDR_ANY;
			return 0;
		}

		if (!inet_pton(ss->ss_family, str, &((struct sockaddr_in *)ss)->sin_addr)) {
			struct hostent *he = gethostbyname(str);

			if (he == NULL) {
				err->len = snprintf(err->msg, err->size, "Invalid server name: '%s'\n", str);
				return -1;
			}
			((struct sockaddr_in *)ss)->sin_addr = *(struct in_addr *) *(he->h_addr_list);
		}
	}

	return 0;
}

/*
 * returns the difference, in microseconds, between tv1 and tv2, which must
 * be ordered.
 */
static inline long long tv_diff(struct timeval *tv1, struct timeval *tv2)
{
	long long ret;

	ret = (long long)(tv2->tv_sec - tv1->tv_sec) * 1000000LL;
	ret += tv2->tv_usec - tv1->tv_usec;
	return ret;
}

void wait_micro(struct timeval *from, unsigned long long delay)
{
	struct timeval end, now;
	unsigned int remain;

	end.tv_sec = from->tv_sec + delay / 1000000;
	end.tv_usec = from->tv_usec + delay % 1000000;
	while (end.tv_usec >= 1000000) {
		end.tv_usec -= 1000000;
		end.tv_sec++;
	}

	while (1) {
		gettimeofday(&now, NULL);
		if (now.tv_sec > end.tv_sec)
			break;
		if (now.tv_sec == end.tv_sec && now.tv_usec >= end.tv_usec)
			break;
		remain = tv_diff(&now, &end);
		if (remain >= 10000)
			usleep(9000);
		/* otherwise do active wait */
	}
}

#define SET_IOV(IOV, IST) ({		\
	(IOV).iov_base = (IST).ptr;	\
	(IOV).iov_len  = (IST).len;	\
	(IST).len;  /* return size */	\
})

#define ADD_IOV(IOV, CNT, IST) ({		\
	(IOV)[CNT].iov_base = (IST).ptr;	\
	(IOV)[CNT].iov_len  = (IST).len;	\
	(CNT)++;				\
	(IST).len;  /* return size */		\
})

void flood(int fd, struct sockaddr_storage *to, int tolen)
{
	struct timeval start, now;
	unsigned long long pkt;
	unsigned long long totbit = 0;
	struct iovec iovec[256]; // max: ~4 + 1 + 15*14 + 1
	struct msghdr msghdr;
	char counter_buf[30];
	long long diff = 0;
	unsigned int x;
	int hdr_len, hdr_num;
	int len = 0;
	int cnt;

	struct ist prio = ist(log_prio);
	struct ist date = ist(log_date);
	struct ist host = ist(log_host);
	struct ist tag  = ist(log_tag);
	struct ist counter;
	struct ist lf = IST("\n");

	msghdr.msg_iov     = iovec;
	msghdr.msg_iovlen  = 0;
	msghdr.msg_name    = NULL; // use connected address
	msghdr.msg_namelen = 0;
	msghdr.msg_control = NULL;
	msghdr.msg_controllen = 0;
	msghdr.msg_flags = 0;

	hdr_len = 0;
	hdr_len += ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, prio);
	hdr_len += ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, date);
	if (host.len > 1)
		hdr_len += ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, host);
	hdr_len += ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, tag);

	hdr_num = msghdr.msg_iovlen; // for easier resets

	gettimeofday(&start, NULL);

	for (pkt = 0; pkt < count; pkt++) {
		while ((pkt && pktrate) || bitrate) {
			gettimeofday(&now, NULL);
			diff = tv_diff(&start, &now);
			if (diff <= 0)
				diff = 1;
			if (pktrate && pkt * 1000000UL > diff * (unsigned long long)pktrate)
				wait_micro(&now, pkt * 1000000ULL / pktrate - diff);
			else if (bitrate && totbit * 1000000UL > diff * (unsigned long long)bitrate)
				wait_micro(&now, totbit * 1000000ULL / bitrate - diff);
			else
				break;
		}

		msghdr.msg_iovlen = hdr_num;
		len = hdr_len;

		/* write the counter in ASCII and replace the trailing zero with a space */
		counter.ptr = utoa(pkt, counter_buf, sizeof(counter_buf));
		counter_buf[sizeof(counter_buf) - 1] = ' ';
		counter.len = counter_buf + sizeof(counter_buf) - counter.ptr;
		len += ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, counter);

		// generate random fields, multiple times if needed
		x = statistical_prng();
		cnt = x >> 28;
		//cnt = 0;
		do {
			int idx, form;

			for (idx = 0; idx < 14; idx++) {
				form = (x >> (idx*2)) & 3;
				if (form)
					len += ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, fields[idx][form-(cnt&1)]);
			}
			if (cnt)
				x = statistical_prng();
		} while (cnt--);

		/* and finally the LF */
		len += ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, lf);

		if (pkt >= start_num)
			if (sendmsg(fd, &msghdr, MSG_NOSIGNAL | MSG_DONTWAIT) >= 0)
				totbit += (len + 28) * 8;
	}

	printf("%llu packets sent in %lld us\n", pkt, diff);
}

int main(int argc, char **argv)
{
	struct sockaddr_storage ss;
	struct errmsg err;
	char hostname[256];
	char *prog = *argv;
	int addrlen;
	int fd;

	if (gethostname(hostname, sizeof(hostname) - 1) == 0) {
		hostname[strlen(hostname) + 1] = 0;
		hostname[strlen(hostname)] = ' ';
		log_host = hostname;
	}

	err.len = 0;
	err.size = 100;
	err.msg = malloc(err.size);

	--argc; ++argv;

	while (argc && **argv == '-') {
		if (strcmp(*argv, "-t") == 0) {
			address = *++argv;
			argc--;
		}
		else if (strcmp(*argv, "-r") == 0) {
			pktrate = atol(*++argv);
			argc--;
		}
		else if (strcmp(*argv, "-b") == 0) {
			bitrate = atol(*++argv);
			argc--;
		}
		else if (strcmp(*argv, "-n") == 0) {
			count = atol(*++argv);
			argc--;
		}
		else if (strcmp(*argv, "-s") == 0) {
			start_num = atol(*++argv);
			argc--;
		}
		else if (strcmp(*argv, "-h") == 0) {
			strncpy(hostname, *++argv, sizeof(hostname) - 1);
			hostname[strlen(hostname) + 1] = 0;
			hostname[strlen(hostname)] = ' ';
			log_host = hostname;
			argc--;
		}
		else
			break;
		argc--;
		argv++;
	}

	if (argc > 0 || !*address) {
		fprintf(stderr,
			"usage: %s [ -t address:port ] [ -r pktrate ]\n"
			"          [ -b bitrate] [ -s start ] [ -n count ]\n"
			"          [ -h hostname ]\n", prog);
		exit(1);
	}

	if (addr_to_ss(address, &ss, &err) < 0) {
		fprintf(stderr, "%s\n", err.msg);
		exit(1);
	}

	addrlen = (ss.ss_family == AF_INET6) ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);

	if ((fd = socket(ss.ss_family, SOCK_DGRAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) == -1) {
		perror("setsockopt(SO_REUSEADDR)");
		exit(1);
	}

	if (connect(fd, (struct sockaddr *)&ss, addrlen) == -1) {
		perror("connect()");
		exit(1);
	}

	flood(fd, &ss, addrlen);

	return 0;
}
