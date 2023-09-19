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

struct errmsg {
	char *msg;
	int size;
	int len;
};

const int zero = 0;
const int one = 1;

/* roughly 5 kB of random text */
const char *lorem =
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Aliquam "
	"tempus fringilla ullamcorper. Vestibulum ante ipsum primis in "
	"faucibus orci luctus et ultrices posuere cubilia curae; Aliquam "
	"quis risus dictum, eleifend dolor a, venenatis sem. Vestibulum "
	"ante ipsum primis in faucibus orci luctus et ultrices posuere "
	"cubilia curae; Nulla molestie hendrerit varius. Cras quis mauris "
	"at nunc sodales egestas. Vivamus bibendum nec quam et "
	"bibendum. Morbi quis arcu porttitor, eleifend nisi blandit, "
	"aliquam neque. Cras mollis eu felis quis tincidunt. Nam non nibh "
	"ipsum. Sed consequat nulla eget tortor dapibus iaculis. Vivamus "
	"aliquet, turpis sit amet eleifend cursus, velit nibh elementum "
	"urna, suscipit faucibus lectus justo et lacus. Nulla at lectus "
	"volutpat, facilisis metus vel, porta est. Aliquam in eros a urna "
	"suscipit dignissim at eu leo. Nunc congue sem at urna egestas "
	"tincidunt. Nullam at pulvinar velit, a dictum est. Aliquam id "
	"sollicitudin purus. Ut aliquam maximus viverra. Pellentesque "
	"tristique risus eros, malesuada viverra lorem bibendum at. Aliquam "
	"mattis lacus quis massa maximus malesuada. Proin ut sem nec est "
	"dignissim dignissim. Donec sagittis erat lobortis arcu pretium "
	"porttitor. Donec eu tellus sodales felis rutrum rhoncus. Aliquam "
	"malesuada neque ligula, interdum imperdiet enim varius tincidunt. "
	"Nulla condimentum euismod tortor quis sollicitudin. Duis massa "
	"arcu, sagittis vitae velit et, rutrum consectetur risus. "
	"Suspendisse bibendum gravida fermentum. Proin rutrum at dui at "
	"commodo. Nulla vitae laoreet arcu. Proin imperdiet tortor eget "
	"massa lacinia auctor. Pellentesque quis congue lacus. Fusce id "
	"augue quam. Duis eget erat sed turpis scelerisque porttitor ac "
	"lobortis sem. Duis at mauris vitae urna volutpat fringilla. Etiam "
	"viverra nibh nisl, at cursus orci mollis vitae. Aliquam bibendum "
	"tortor eu nunc vestibulum, finibus vehicula elit porttitor. "
	"Aliquam erat volutpat. Vestibulum a ex vel magna finibus gravida "
	"in at tellus. Phasellus a tincidunt est. Suspendisse potenti. "
	"Quisque mauris mi, mattis ut libero vitae, scelerisque condimentum "
	"lorem. Duis bibendum neque id ex eleifend luctus. Aliquam erat "
	"volutpat. Orci varius natoque penatibus et magnis dis parturient "
	"montes, nascetur ridiculus mus. Integer enim turpis, auctor vel "
	"venenatis sollicitudin, scelerisque id purus. Proin elementum "
	"consequat rhoncus. Aenean posuere felis eget placerat lacinia. "
	"Nam laoreet luctus velit vitae ultrices. Mauris porta feugiat "
	"ante, ut tincidunt velit pretium vitae. Sed convallis ante nec "
	"egestas eleifend. Sed imperdiet justo ut auctor bibendum. Donec "
	"ligula sem, finibus nec tellus at, gravida lobortis tellus. "
	"Curabitur posuere magna nisl. Mauris dignissim volutpat "
	"ullamcorper. Nulla gravida et nisl sed vestibulum. Duis libero "
	"metus, pellentesque nec porttitor vel, dictum at enim. Mauris "
	"luctus risus eget ligula porta tempus. Quisque eu egestas sem. "
	"Pellentesque ac arcu molestie, vulputate velit ut, pellentesque "
	"dui. Nam id consequat quam. Aenean sit amet tellus a enim "
	"imperdiet sodales. In vestibulum ut massa eget tempus. Curabitur "
	"eu luctus elit, quis cursus odio. Nullam porta, urna a dapibus "
	"elementum, purus enim porttitor enim, a ultrices enim ex id sem. "
	"Maecenas varius ullamcorper felis, sit amet ullamcorper libero "
	"volutpat a. Suspendisse accumsan lacinia dolor, sit amet lobortis "
	"urna mattis ac. Duis vitae aliquam lorem, a porttitor odio. "
	"Vestibulum eget magna vitae leo feugiat accumsan a et nisi. Nam "
	"pretium nec orci ac volutpat. Aliquam sodales, justo at ultricies "
	"porta, felis dolor placerat purus, sit amet faucibus leo nisi et "
	"erat. Sed faucibus tincidunt libero, ac venenatis ante malesuada "
	"eget. Orci varius natoque penatibus et magnis dis parturient "
	"montes, nascetur ridiculus mus. Sed mi purus, lobortis sit amet "
	"leo at, venenatis elementum tortor. In quis ultrices dui. Cras "
	"nec elementum nulla. Aliquam imperdiet, mauris et elementum "
	"hendrerit, nibh ante commodo nisl, vel iaculis est purus malesuada "
	"sem. Quisque et nulla metus. Ut ullamcorper placerat nisi. Ut "
	"rhoncus mollis fringilla. Etiam urna tellus, ullamcorper id "
	"lobortis eget, finibus id nisl. Sed id nunc rutrum, ullamcorper "
	"odio in, mattis eros. Sed et condimentum eros, at molestie est. "
	"Ut eu nulla sit amet lorem molestie sagittis. Nullam cursus massa "
	"a nulla dictum, ac fermentum nibh ornare. Donec ut efficitur "
	"libero. Nunc odio ipsum, facilisis et erat et, viverra placerat "
	"neque. Proin id dignissim justo, in efficitur tortor. Curabitur "
	"eget nisl lectus. Morbi eu arcu ut nulla accumsan tincidunt. "
	"Mauris malesuada sed metus vitae pulvinar. Aliquam maximus elit "
	"nec nunc malesuada, eu porttitor urna vestibulum. Nullam "
	"placerat, eros in vestibulum maximus, mauris ligula dapibus orci, "
	"eu tincidunt urna tortor eget enim. Aliquam aliquet tortor quis "
	"arcu tincidunt, nec fringilla lectus accumsan. Aenean semper, "
	"purus malesuada sagittis euismod, velit eros varius nunc, et "
	"vehicula velit risus et elit. Donec ut fringilla neque, vitae "
	"lacinia dui. Morbi aliquam lectus mauris, eget placerat leo "
	"mollis vel. In purus ligula, tristique quis posuere sodales, "
	"ultricies eu nulla. Vivamus et lorem sed mi pulvinar varius sit "
	"amet ac dolor. Vestibulum id mollis nibh, ut consectetur turpis. "
	"Donec interdum mattis lectus tincidunt interdum. Pellentesque "
	"aliquam maximus tellus, quis laoreet ante imperdiet et. Lorem "
	"ipsum dolor sit justo.\n";

/* used to create a date */
const char *monthname[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

/* fields used by syslog, all must contain the trailing space */
int log_prio = 134; // LOG_LOCAL0 + LOG_INFO
const char *log_host = "localhost ";
const char *log_tag  = "loggen:";

static unsigned int cfg_bitrate;
static unsigned int cfg_pktrate;
static int count = 1;
static char *address = "";
unsigned int statistical_prng_state = 0x12345678;

/* current time */
struct timeval now;

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
	struct timeval end;
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

#define ADD_IOV(IOV, CNT, PTR, LEN) ({			\
	(IOV)[CNT].iov_base = (PTR);			\
	(IOV)[CNT].iov_len  = (LEN);			\
	(CNT)++;					\
	(IOV)[(CNT)-1].iov_len;  /* return size */	\
})

void flood(int fd, struct sockaddr_storage *to, int tolen)
{
	struct timeval start;
	unsigned long long pkt;
	unsigned long long totbit = 0;
	struct iovec iovec[3]; // hdr, counter, msg
	struct msghdr msghdr;
	char counter_buf[30];
	char *counter_ptr;
	int counter_len;
	long long diff = 0;
	unsigned int x;
	int hdr_len;
	int lorem_len = strlen(lorem);
	const char *lorem_end = lorem + lorem_len;
	time_t prev_sec = 0;
	char hdr[256];
	int len = 0;

	msghdr.msg_iov     = iovec;
	msghdr.msg_iovlen  = 0;
	msghdr.msg_name    = NULL; // use connected address
	msghdr.msg_namelen = 0;
	msghdr.msg_control = NULL;
	msghdr.msg_controllen = 0;
	msghdr.msg_flags = 0;

	hdr_len = 0;

	gettimeofday(&start, NULL);
	gettimeofday(&now, NULL);

	for (pkt = 0; pkt < count; pkt++) {
		if (!cfg_bitrate && !cfg_pktrate && (pkt & 63) == 0)
			gettimeofday(&now, NULL); // get time once in a while at least

		while ((pkt && cfg_pktrate) || cfg_bitrate) {
			gettimeofday(&now, NULL);
			diff = tv_diff(&start, &now);
			if (diff <= 0)
				diff = 1;
			if (cfg_pktrate && pkt * 1000000UL > diff * (unsigned long long)cfg_pktrate)
				wait_micro(&now, pkt * 1000000ULL / cfg_pktrate - diff);
			else if (cfg_bitrate && totbit * 1000000UL > diff * (unsigned long long)cfg_bitrate)
				wait_micro(&now, totbit * 1000000ULL / cfg_bitrate - diff);
			else
				break;
		}

		if (now.tv_sec != prev_sec) {
			/* time changed, rebuild the header */
			struct tm tm;

			prev_sec = now.tv_sec;
			localtime_r(&now.tv_sec, &tm);

			hdr_len = snprintf(hdr, sizeof(hdr), "<%d> %s %2d %02d:%02d:%02d %s%s ",
					   log_prio, monthname[tm.tm_mon], tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
					   log_host[1] ? log_host : "", log_tag);
		}

		msghdr.msg_iovlen = 0;
		len = ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, hdr, hdr_len);

		/* write the counter in ASCII and replace the trailing zero with a space */
		counter_ptr = utoa(pkt, counter_buf, sizeof(counter_buf));
		counter_buf[sizeof(counter_buf) - 1] = ' ';
		counter_len = counter_buf + sizeof(counter_buf) - counter_ptr;
		len += ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, counter_ptr, counter_len);

		/* append random-sized text, terminated with LF. Let's first
		 * produce a cubic random from 0 to 4286583807 (~2^32) centered
		 * around 536870912 (2^29), then we scale it down to the text
		 * length. This means that for a 5.3kB text, the avg message
		 * size will be around 660 bytes long.
		 */
		x = statistical_prng();
		x = (x >> 22) * ((x >> 11) & 2047) * (x & 2047);
		x = mul32hi(x, lorem_len - 2) + 1;

		len += ADD_IOV(msghdr.msg_iov, msghdr.msg_iovlen, (char *)lorem_end - x, x);

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
			cfg_pktrate = atol(*++argv);
			argc--;
		}
		else if (strcmp(*argv, "-b") == 0) {
			cfg_bitrate = atol(*++argv);
			argc--;
		}
		else if (strcmp(*argv, "-n") == 0) {
			count = atol(*++argv);
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
			"          [ -b bitrate] [ -n count ]\n"
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
